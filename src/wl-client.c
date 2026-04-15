#define _GNU_SOURCE

#include "wl-client.h"
#include "debug.h"
#include "macro_magic.h"

#include <unistd.h>
#include <string.h>

#include "types.h"
#include "xdg-shell-client-protocol.h"
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-util.h>

#define ENSURE_OR_GOTO_ERR(expr)                                                    \
do {                                                                                \
    if (!(expr)) {                                                                  \
        _wlclient_report_wayland_fatal(g_state.display, #expr, __FILE__, __LINE__); \
        goto error;                                                                 \
    }                                                                               \
} while(0)

static wlclient_global_state g_state;

//======================================================================================================================
// Helper Declarations
//======================================================================================================================

static void set_allocator(wlclient_allocator* allocator);

static void destroy_all_input_devices(void);
static void destroy_input_device(wlclient_input_device* input_device);

static void store_new_input_device(u32 id, struct wl_seat* seat);
static wlclient_input_device* find_input_device_by_seat(struct wl_seat* seat);

//======================================================================================================================
// Wayland Handlers
//======================================================================================================================

static void register_global(void* data, struct wl_registry* wl_registry, u32 id, const char* interface, u32 version);
static void register_global_remove(void* data, struct wl_registry* wl_registry, u32 id);

static void shm_pick_format(void *data, struct wl_shm *wl_shm, u32 format);

static void xdg_wm_base_ping(void* data, struct xdg_wm_base* xdg_wm_base, u32 serial);

static void seat_capabilities(void *data, struct wl_seat *wl_seat, u32 capabilities);
static void seat_name(void *data, struct wl_seat *wl_seat, const char *name);

static void pointer_enter(void* data, struct wl_pointer* wl_pointer, u32 serial, struct wl_surface* surface, wl_fixed_t surface_x, wl_fixed_t surface_y);
static void pointer_leave(void* data, struct wl_pointer* wl_pointer, u32 serial, struct wl_surface* surface);
static void pointer_motion(void* data, struct wl_pointer* wl_pointer, u32 time, wl_fixed_t surface_x, wl_fixed_t surface_y);
static void pointer_button(void* data, struct wl_pointer* wl_pointer, u32 serial, u32 time, u32 button, u32 state);
static void pointer_axis(void* data, struct wl_pointer* wl_pointer, u32 time, u32 axis, wl_fixed_t value);
static void pointer_frame(void* data, struct wl_pointer* wl_pointer);
static void pointer_axis_source(void* data, struct wl_pointer* wl_pointer, u32 axis_source);
static void pointer_axis_stop(void* data, struct wl_pointer* wl_pointer, u32 time, u32 axis);
static void pointer_axis_discrete(void* data, struct wl_pointer* wl_pointer, u32 axis, i32 discrete);
static void pointer_axis_value120(void* data, struct wl_pointer* wl_pointer, u32 axis, i32 value120);
static void pointer_axis_relative_direction(void* data, struct wl_pointer* wl_pointer, u32 axis, u32 direction);

static void keyboard_keymap(void* data, struct wl_keyboard* wl_keyboard, u32 format, i32 fd, u32 size);
static void keyboard_enter(void* data, struct wl_keyboard* wl_keyboard, u32 serial, struct wl_surface* surface, struct wl_array* keys);
static void keyboard_leave(void* data, struct wl_keyboard* wl_keyboard, u32 serial, struct wl_surface* surface);
static void keyboard_key(void* data, struct wl_keyboard* wl_keyboard, u32 serial, u32 time, u32 key, u32 state);
static void keyboard_modifiers(void* data, struct wl_keyboard* wl_keyboard, u32 serial, u32 mods_depressed, u32 mods_latched, u32 mods_locked, u32 group);
static void keyboard_repeat_info(void* data, struct wl_keyboard* wl_keyboard, i32 rate, i32 delay);

//======================================================================================================================
// PUBLIC
//======================================================================================================================

wlclient_error_code wlclient_init(wlclient_allocator* allocator) {
    WLCLIENT_LOG_INFO("Initializing...");

    set_allocator(allocator);

    i32 ret = 0;

    g_state.display = wl_display_connect(NULL);
    if (!g_state.display) {
        WLCLIENT_LOG_FATAL("Failed to connect to display");
        return WLCLIENT_ERROR_INIT_FAILED;
    }

    // Setup the global registry
    {
        g_state.registry = wl_display_get_registry(g_state.display);
        ENSURE_OR_GOTO_ERR(g_state.registry);

        static const struct wl_registry_listener registry_listener = {
            .global = register_global,
            .global_remove = register_global_remove,
        };
        ret = wl_registry_add_listener(g_state.registry, &registry_listener, NULL);
        ENSURE_OR_GOTO_ERR(ret == 0);

        // Block until all pending request to discover globals are processed.
        ret = wl_display_roundtrip(g_state.display);
        ENSURE_OR_GOTO_ERR(ret >= 0);
    }

    // Verify all necessary globals are registered.
    ENSURE_OR_GOTO_ERR(g_state.compositor);
    ENSURE_OR_GOTO_ERR(g_state.subcompositor);
    ENSURE_OR_GOTO_ERR(g_state.xdgWmBase);
    ENSURE_OR_GOTO_ERR(g_state.shm);

    // Do a second roundtrip to receive events emitted by newly bound globals (like wl_shm preferred pixel format).
    ret = wl_display_roundtrip(g_state.display);
    ENSURE_OR_GOTO_ERR(ret >= 0);

    ENSURE_OR_GOTO_ERR(g_state.preffered_pixel_format >= 0);

    // Verify device inputs are configured correctly:
    {
        ENSURE_OR_GOTO_ERR(g_state.input_devices_count > 0);
        for (i32 i = 0; i < g_state.input_devices_count; i++) {
            wlclient_input_device* d = &g_state.input_devices[i];
            ENSURE_OR_GOTO_ERR(d->used);
            ENSURE_OR_GOTO_ERR(d->seat);
            ENSURE_OR_GOTO_ERR(d->seat_id);
            ENSURE_OR_GOTO_ERR(d->pointer);
            ENSURE_OR_GOTO_ERR(d->keyboard);
        }
    }

    WLCLIENT_LOG_INFO("Initialization done");
    return WLCLIENT_ERROR_OK;

error:
    wlclient_shutdown();
    return WLCLIENT_ERROR_INIT_FAILED;
}

void wlclient_shutdown(void) {
    WLCLIENT_LOG_INFO("Shutting down...");

    if (g_state.backend_shutdown) g_state.backend_shutdown();

    destroy_all_input_devices();

    if (g_state.compositor) wl_compositor_destroy(g_state.compositor);
    if (g_state.subcompositor) wl_subcompositor_destroy(g_state.subcompositor);
    if (g_state.xdgWmBase) xdg_wm_base_destroy(g_state.xdgWmBase);
    if (g_state.shm) wl_shm_destroy(g_state.shm);
    if (g_state.registry) wl_registry_destroy(g_state.registry);
    if (g_state.display) wl_display_disconnect(g_state.display);

    memset(&g_state, 0, sizeof(g_state));
    g_state.preffered_pixel_format = -1;

    WLCLIENT_LOG_INFO("Shutdown done");
}

struct wl_display* _wlclient_get_wl_display(void) {
    WLCLIENT_ASSERT(g_state.display, "display is null");
    return g_state.display;
}

struct wlclient_global_state* _wlclient_get_wl_global_state(void) {
    return &g_state;
}

wlclient_window_data* wlclient_get_wl_window_data(const wlclient_window* window) {
    WLCLIENT_ASSERT(window && window->id >= 0 && window->id < WLCLIENT_WINDOWS_COUNT, "Invalid window argument");
    wlclient_window_data* ret = &g_state.windows[window->id];
    WLCLIENT_ASSERT(ret->used, "Window is marked as unused");
    return ret;
}

void _wlclient_set_backend_shutdown(void (*shutdown)(void)) {
    g_state.backend_shutdown = shutdown;
}

void _wlclient_set_backend_destroy_window(void (*destroy_window)(const wlclient_window* window)) {
    g_state.backend_destroy_window = destroy_window;
}

void _wlclient_set_backend_resize_window(void (*resize_window)(const wlclient_window* window, i32 framebuffer_width, i32 framebuffer_height)) {
    g_state.backend_resize_window = resize_window;
}

//======================================================================================================================
// Helper Implementations
//======================================================================================================================

static void set_allocator(wlclient_allocator* allocator) {
    g_state.allocator.alloc = allocator && allocator->alloc ? allocator->alloc : malloc;
    g_state.allocator.strdup = allocator && allocator->strdup ? allocator->strdup : strdup;
}

static void destroy_all_input_devices(void) {
    WLCLIENT_LOG_INFO("Destroying all input devices...");

    for (i32 i = 0; i < g_state.input_devices_count; i++) {
        struct wlclient_input_device* input_device = &g_state.input_devices[i];
        destroy_input_device(input_device);
    }

    g_state.input_devices_count = 0;

    WLCLIENT_LOG_INFO("Input devices destroyed");
}

static void destroy_input_device(wlclient_input_device* input_device) {
    if (input_device->pointer)   wl_pointer_release(input_device->pointer);
    if (input_device->keyboard)  wl_keyboard_release(input_device->keyboard);
    if (input_device->seat_name) free(input_device->seat_name);
    if (input_device->seat)      wl_seat_destroy(input_device->seat);

    // Marks the pointer as unused along with zeroing out everything else:
    memset(input_device, 0, sizeof(*input_device));
}

static void store_new_input_device(u32 id, struct wl_seat* seat) {
    WLCLIENT_PANIC(
        g_state.input_devices_count < WLCLIENT_MAX_INPUT_DEVICES,
        "Maximum allowed seats (%"PRIi32") exceeded.",
        WLCLIENT_MAX_INPUT_DEVICES
    );

    wlclient_input_device new_device = {
        .seat_id = id,
        .seat = seat,
        .used = true
    };

    g_state.input_devices[g_state.input_devices_count] = new_device;
    g_state.input_devices_count++;
}

static wlclient_input_device* find_input_device_by_seat(struct wl_seat* seat) {
    for (i32 i = 0; i < g_state.input_devices_count; i++) {
        if (g_state.input_devices[i].seat == seat && g_state.input_devices[i].used) {
            return &g_state.input_devices[i];
        }
    }

    WLCLIENT_PANIC(false, "[BUG] Find failed; seat not registered");
    return NULL;
}


//======================================================================================================================
// Wayland Handlers Implementations
//======================================================================================================================

/**
* This is the mechanism for discovering compositor capabilities. Called when the compositor announces a new global
* interface.
*
* This happens:
*   - during initial registry enumeration (after roundtrip)
*   - when new globals appear at runtime
*
* Parameters:
*   data      - user-provided pointer passed to wl_registry_add_listener
*   registry  - the wl_registry instance
*   id        - unique numeric identifier for this global
*   interface - string name of the interface (e.g. "wl_compositor", "wl_seat")
*   version   - maximum supported version of the interface
*/
static void register_global(void* data, struct wl_registry* wl_registry, u32 id, const char* interface, u32 version) {
    (void) data;

    WLCLIENT_LOG_TRACE("Register global received for interface = %s", interface);

    if (strcmp(interface, "wl_compositor") == 0) {
        WLCLIENT_PANIC(!g_state.compositor, "wl_compositor re-registered");
        u32 effective_version = WLCLIENT_MIN((u32) wl_compositor_interface.version, version);
        g_state.compositor = wl_registry_bind(wl_registry, id, &wl_compositor_interface, effective_version);
        WLCLIENT_PANIC(g_state.compositor, "failed to create wl_compositor");
    }
    else if (strcmp(interface, "wl_subcompositor") == 0) {
        WLCLIENT_PANIC(!g_state.subcompositor, "wl_subcompositor re-registered");
        u32 effective_version = WLCLIENT_MIN((u32) wl_subcompositor_interface.version, version);
        g_state.subcompositor = wl_registry_bind(wl_registry, id, &wl_subcompositor_interface, effective_version);
        WLCLIENT_PANIC(g_state.subcompositor, "failed to create wl_subcompositor");
    }
    else if (strcmp(interface, "xdg_wm_base") == 0) {
        WLCLIENT_PANIC(!g_state.xdgWmBase, "xdg_wm_base re-registered");
        u32 effective_version = WLCLIENT_MIN((u32) xdg_wm_base_interface.version, version);
        g_state.xdgWmBase = wl_registry_bind(wl_registry, id, &xdg_wm_base_interface, effective_version);
        WLCLIENT_PANIC(g_state.xdgWmBase, "failed to create xdg_wm_base");

        static const struct xdg_wm_base_listener listender = {
            .ping = xdg_wm_base_ping
        };
        i32 ret = xdg_wm_base_add_listener(g_state.xdgWmBase, &listender, NULL);
        WLCLIENT_PANIC(ret == 0, "failed to setup xdg base ping listener");
    }
    else if (strcmp(interface, "wl_shm") == 0) {
        WLCLIENT_PANIC(!g_state.shm, "wl_shm re-registered");

        u32 effective_version = WLCLIENT_MIN((u32) wl_shm_interface.version, version);
        g_state.shm = wl_registry_bind(wl_registry, id, &wl_shm_interface, effective_version);
        WLCLIENT_PANIC(g_state.shm, "failed to create wl_shm");

        static const struct wl_shm_listener listener = {
            .format = shm_pick_format
        };
        i32 ret = wl_shm_add_listener(g_state.shm, &listener, NULL);
        WLCLIENT_PANIC(ret == 0, "failed to setup shm format listener");
    }
    else if (strcmp(interface, "wl_seat") == 0) {
        u32 effective_version = WLCLIENT_MIN((u32) wl_seat_interface.version, version);
        struct wl_seat* seat = wl_registry_bind(wl_registry, id, &wl_seat_interface, effective_version);
        if (!seat) return;

        static const struct wl_seat_listener listener = {
            .capabilities = seat_capabilities,
            .name = seat_name,
        };
        i32 ret = wl_seat_add_listener(seat, &listener, NULL);
        WLCLIENT_PANIC(ret == 0, "Failed to add seat listener");

        store_new_input_device(id, seat);
    }
}

/**
* This is the mechanism for learning that a previously advertised global interface has been removed from the registry.
*
* This happens:
*   - when a global disappears at runtime
*
* Parameters:
*   data      - user-provided pointer passed to wl_registry_add_listener
*   registry  - the wl_registry instance
*   id        - unique numeric identifier for the removed global
*/
static void register_global_remove(void* data, struct wl_registry* wl_registry, u32 id) {
    (void)data;
    (void)wl_registry;
    WLCLIENT_LOG_DEBUG("Removing %" PRIu32, id);
}

/**
* This advertises one supported shared-memory pixel format for wl_shm buffers.
*
* This happens:
*   - after binding the wl_shm global
*   - once for each pixel format supported by the compositor
*
* Parameters:
*   data   - user-provided pointer passed to wl_shm_add_listener
*   wl_shm - the wl_shm instance
*   format - one advertised wl_shm_format value supported by the compositor
*/
static void shm_pick_format(void *data, struct wl_shm *wl_shm, u32 format) {
    (void)data;
    (void)wl_shm;

    WLCLIENT_LOG_TRACE("Supported pixel format %" PRIu32, format);

    // The spec states that ARGB8888 and XRGB8888 should be supported by all renderers, so it is safe to just pick
    // ARGB8888 and never bother with anything else.
    if (format == WL_SHM_FORMAT_ARGB8888) {
        g_state.preffered_pixel_format = (i32) WL_SHM_FORMAT_ARGB8888;
        WLCLIENT_LOG_DEBUG("Pixel format set to %" PRIu32, format);
    }
}

/**
* This is the compositor liveness check for the xdg_wm_base object. The client must respond with a pong.
*
* This happens:
*   - when the compositor verifies that the client is responsive
*
* Parameters:
*   data        - user-provided pointer passed to xdg_wm_base_add_listener
*   xdg_wm_base - the xdg_wm_base instance
*   serial      - serial number that must be echoed back in xdg_wm_base_pong
*/
static void xdg_wm_base_ping(void* data, struct xdg_wm_base* xdg_wm_base, u32 serial) {
    (void)data;

    WLCLIENT_LOG_TRACE("Ping received serial: %" PRIu32, serial);
    xdg_wm_base_pong(xdg_wm_base, serial);
}

/**
* This reports which input device classes are currently exposed by the seat.
*
* This happens:
*   - after binding the wl_seat global
*   - whenever the seat gains or loses capabilities such as pointer or keyboard
*
* Parameters:
*   data         - user-provided pointer passed to wl_seat_add_listener
*   wl_seat      - the wl_seat instance
*   capabilities - bitmask of wl_seat_capability values currently available on the seat
*/
static void seat_capabilities(void *data, struct wl_seat *wl_seat, u32 capabilities) {
    (void)data;

    WLCLIENT_LOG_DEBUG("Seat capabilities: %"PRIu32, capabilities);

     wlclient_input_device* input_device = find_input_device_by_seat(wl_seat);

    if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
        if (!input_device->keyboard) {
            input_device->keyboard = wl_seat_get_keyboard(input_device->seat);
            static const struct wl_keyboard_listener listener = {
                .enter = keyboard_enter,
                .leave = keyboard_leave,
                .key = keyboard_key,
                .keymap = keyboard_keymap,
                .modifiers = keyboard_modifiers,
                .repeat_info = keyboard_repeat_info,
            };
            i32 ret = wl_keyboard_add_listener(input_device->keyboard, &listener, NULL);
            WLCLIENT_PANIC(ret == 0, "Failed to add keyboard listener");

            WLCLIENT_LOG_DEBUG("Registered keyboard for seat(id=%"PRIu32")", input_device->seat_id);
        }
    }
    else {
        if (input_device->keyboard) {
            // Keyboard was unplugged (or compositor revoked the capability).
            WLCLIENT_LOG_DEBUG("Releasing keyboard for seat(id=%"PRIu32")", input_device->seat_id);
            wl_keyboard_release(input_device->keyboard);
            input_device->keyboard = NULL;
        }
        else {
            WLCLIENT_LOG_DEBUG("No keyboard capability for seat(id=%"PRIu32")", input_device->seat_id);
        }
    }

    if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
        if (!input_device->pointer) {
            input_device->pointer = wl_seat_get_pointer(input_device->seat);
            static const struct wl_pointer_listener listener = {
                .enter = pointer_enter,
                .leave = pointer_leave,
                .motion = pointer_motion,
                .button = pointer_button,
                .axis = pointer_axis,
                .frame = pointer_frame,
                .axis_source = pointer_axis_source,
                .axis_stop = pointer_axis_stop,
                .axis_discrete = pointer_axis_discrete,
                .axis_value120 = pointer_axis_value120,
                .axis_relative_direction = pointer_axis_relative_direction,
            };
            i32 ret = wl_pointer_add_listener(input_device->pointer, &listener, NULL);
            WLCLIENT_PANIC(ret == 0, "Failed to add pointer listener");

            WLCLIENT_LOG_DEBUG("Registered mouse for seat(id=%"PRIu32")", input_device->seat_id);
        }
    }
    else {
        if (input_device->pointer) {
            // Mouse was unplugged (or compositor revoked the capability).
            WLCLIENT_LOG_DEBUG("Releasing mouse for seat(id=%"PRIu32")", input_device->seat_id);
            wl_pointer_release(input_device->pointer);
            input_device->pointer = NULL;
        }
        else {
            WLCLIENT_LOG_DEBUG("No mouse capability for seat(id=%"PRIu32")", input_device->seat_id);
        }
    }
}

/**
* This reports the compositor-provided descriptive name for the seat.
*
* This happens:
*   - after binding the wl_seat global when the compositor advertises a seat name
*   - whenever the compositor updates the seat name
*
* Parameters:
*   data    - user-provided pointer passed to wl_seat_add_listener
*   wl_seat - the wl_seat instance
*   name    - human-readable seat name supplied by the compositor
*/
static void seat_name(void *data, struct wl_seat *wl_seat, const char *name) {
    (void)data;

    WLCLIENT_LOG_DEBUG("Seat name: %s", name);
    wlclient_input_device* input_device = find_input_device_by_seat(wl_seat);
    input_device->seat_name = g_state.allocator.strdup(name);
}


/**
* This notifies the client that pointer focus entered one of its surfaces.
*
* This happens:
*   - when the compositor grants pointer focus to the client surface
*
* Parameters:
*   data      - user-provided pointer passed to wl_pointer_add_listener
*   wl_pointer - the wl_pointer instance
*   serial    - serial identifying the enter event
*   surface   - the wl_surface that received pointer focus
*   surface_x - pointer x coordinate in surface-local coordinates
*   surface_y - pointer y coordinate in surface-local coordinates
*/
static void pointer_enter(void* data, struct wl_pointer* wl_pointer, u32 serial, struct wl_surface* surface, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    (void)data;
    (void)wl_pointer;
    (void)surface;

    WLCLIENT_LOG_TRACE(
        "Pointer enter: serial=%" PRIu32 ", x=%f, y=%f",
        serial,
        wl_fixed_to_double(surface_x),
        wl_fixed_to_double(surface_y)
    );
}

/**
* This notifies the client that pointer focus left one of its surfaces.
*
* This happens:
*   - when the compositor removes pointer focus from the client surface
*
* Parameters:
*   data      - user-provided pointer passed to wl_pointer_add_listener
*   wl_pointer - the wl_pointer instance
*   serial    - serial identifying the leave event
*   surface   - the wl_surface that lost pointer focus
*/
static void pointer_leave(void* data, struct wl_pointer* wl_pointer, u32 serial, struct wl_surface* surface) {
    (void)data;
    (void)wl_pointer;
    (void)surface;

    WLCLIENT_LOG_TRACE("Pointer leave: serial=%" PRIu32, serial);
}

/**
* This reports pointer motion in surface-local coordinates.
*
* This happens:
*   - whenever the pointer moves while focused on one of the client's surfaces
*
* Parameters:
*   data      - user-provided pointer passed to wl_pointer_add_listener
*   wl_pointer - the wl_pointer instance
*   time      - compositor timestamp in milliseconds
*   surface_x - pointer x coordinate in surface-local coordinates
*   surface_y - pointer y coordinate in surface-local coordinates
*/
static void pointer_motion(void* data, struct wl_pointer* wl_pointer, u32 time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    (void)data;
    (void)wl_pointer;

    WLCLIENT_LOG_TRACE(
        "Pointer motion: time=%" PRIu32 ", x=%f, y=%f",
        time,
        wl_fixed_to_double(surface_x),
        wl_fixed_to_double(surface_y)
    );
}

/**
* This reports a pointer button press or release.
*
* This happens:
*   - whenever a focused pointer button changes state
*
* Parameters:
*   data      - user-provided pointer passed to wl_pointer_add_listener
*   wl_pointer - the wl_pointer instance
*   serial    - serial identifying the button event
*   time      - compositor timestamp in milliseconds
*   button    - linux input button code
*   state     - wl_pointer_button_state value for press or release
*/
static void pointer_button(void* data, struct wl_pointer* wl_pointer, u32 serial, u32 time, u32 button, u32 state) {
    (void)data;
    (void)wl_pointer;

    WLCLIENT_LOG_TRACE(
        "Pointer button: serial=%" PRIu32 ", time=%" PRIu32 ", button=%" PRIu32 ", state=%" PRIu32,
        serial, time, button, state
    );
}

/**
* This reports continuous scroll-axis motion for the focused pointer.
*
* This happens:
*   - when the compositor sends scroll deltas for a pointer axis
*
* Parameters:
*   data      - user-provided pointer passed to wl_pointer_add_listener
*   wl_pointer - the wl_pointer instance
*   time      - compositor timestamp in milliseconds
*   axis      - wl_pointer_axis value identifying the scroll axis
*   value     - fixed-point axis delta in compositor-defined units
*/
static void pointer_axis(void* data, struct wl_pointer* wl_pointer, u32 time, u32 axis, wl_fixed_t value) {
    (void)data;
    (void)wl_pointer;

    WLCLIENT_LOG_TRACE(
        "Pointer axis: time=%" PRIu32 ", axis=%" PRIu32 ", value=%f",
        time, axis, wl_fixed_to_double(value)
    );
}

/**
* This marks the end of a batch of pointer events.
*
* This happens:
*   - after the compositor groups related pointer events into a frame
*
* Parameters:
*   data      - user-provided pointer passed to wl_pointer_add_listener
*   wl_pointer - the wl_pointer instance
*/
static void pointer_frame(void* data, struct wl_pointer* wl_pointer) {
    (void)data;
    (void)wl_pointer;

    WLCLIENT_LOG_TRACE("Pointer frame");
}

/**
* This identifies the source device that produced the current pointer axis events.
*
* This happens:
*   - when the compositor provides axis source metadata for a scroll sequence
*
* Parameters:
*   data       - user-provided pointer passed to wl_pointer_add_listener
*   wl_pointer - the wl_pointer instance
*   axis_source - wl_pointer_axis_source value describing the source device
*/
static void pointer_axis_source(void* data, struct wl_pointer* wl_pointer, u32 axis_source) {
    (void)data;
    (void)wl_pointer;

    WLCLIENT_LOG_TRACE("Pointer axis source: %" PRIu32, axis_source);
}

/**
* This signals the end of scrolling activity for a specific pointer axis.
*
* This happens:
*   - when a pointer axis sequence terminates
*
* Parameters:
*   data       - user-provided pointer passed to wl_pointer_add_listener
*   wl_pointer - the wl_pointer instance
*   time       - compositor timestamp in milliseconds
*   axis       - wl_pointer_axis value identifying the stopped axis
*/
static void pointer_axis_stop(void* data, struct wl_pointer* wl_pointer, u32 time, u32 axis) {
    (void)data;
    (void)wl_pointer;

    WLCLIENT_LOG_TRACE("Pointer axis stop: time=%" PRIu32 ", axis=%" PRIu32, time, axis);
}

/**
* This reports deprecated discrete scroll steps for a pointer axis.
*
* This happens:
*   - when the compositor provides integer wheel-step data for scrolling
*
* Parameters:
*   data       - user-provided pointer passed to wl_pointer_add_listener
*   wl_pointer - the wl_pointer instance
*   axis       - wl_pointer_axis value identifying the scroll axis
*   discrete   - integer number of discrete scroll steps
*/
static void pointer_axis_discrete(void* data, struct wl_pointer* wl_pointer, u32 axis, i32 discrete) {
    (void)data;
    (void)wl_pointer;

    WLCLIENT_LOG_TRACE("Pointer axis discrete: axis=%" PRIu32 ", discrete=%" PRIi32, axis, discrete);
}

/**
* This reports high-resolution discrete scroll data in units of 120 per wheel detent.
*
* This happens:
*   - when the compositor sends wl_pointer.axis_value120 events
*
* Parameters:
*   data       - user-provided pointer passed to wl_pointer_add_listener
*   wl_pointer - the wl_pointer instance
*   axis       - wl_pointer_axis value identifying the scroll axis
*   value120   - high-resolution discrete delta where 120 equals one notch
*/
static void pointer_axis_value120(void* data, struct wl_pointer* wl_pointer, u32 axis, i32 value120) {
    (void)data;
    (void)wl_pointer;

    WLCLIENT_LOG_TRACE("Pointer axis value120: axis=%" PRIu32 ", value120=%" PRIi32, axis, value120);
}

/**
* This reports whether the physical pointer scrolling direction matches the logical axis direction.
*
* This happens:
*   - when the compositor provides relative-direction metadata for a pointer axis
*
* Parameters:
*   data       - user-provided pointer passed to wl_pointer_add_listener
*   wl_pointer - the wl_pointer instance
*   axis       - wl_pointer_axis value identifying the scroll axis
*   direction  - wl_pointer_axis_relative_direction value for the axis
*/
static void pointer_axis_relative_direction(void* data, struct wl_pointer* wl_pointer, u32 axis, u32 direction) {
    (void)data;
    (void)wl_pointer;

    WLCLIENT_LOG_TRACE("Pointer axis relative direction: axis=%" PRIu32 ", direction=%" PRIu32, axis, direction);
}

/**
* This advertises the keymap used by the compositor for a keyboard.
*
* This happens:
*   - after binding a wl_keyboard
*   - whenever the compositor updates the keymap
*
* Parameters:
*   data        - user-provided pointer passed to wl_keyboard_add_listener
*   wl_keyboard - the wl_keyboard instance
*   format      - wl_keyboard_keymap_format value
*   fd          - file descriptor for the keymap payload
*   size        - size in bytes of the keymap payload
*/
static void keyboard_keymap(void* data, struct wl_keyboard* wl_keyboard, u32 format, i32 fd, u32 size) {
    (void)data;
    (void)wl_keyboard;
    (void)size;

    // TODO: The keymap is ignored for now; close the fd to avoid leaking it.
    if (fd >= 0) close(fd);

    WLCLIENT_LOG_TRACE("Keyboard keymap: format=%" PRIu32, format);
}

/**
* This notifies the client that keyboard focus entered one of its surfaces.
*
* This happens:
*   - when the compositor grants keyboard focus to the client surface
*
* Parameters:
*   data        - user-provided pointer passed to wl_keyboard_add_listener
*   wl_keyboard - the wl_keyboard instance
*   serial      - serial identifying the enter event
*   surface     - the wl_surface that received keyboard focus
*   keys        - array of keycodes already pressed when focus was entered
*/
static void keyboard_enter(void* data, struct wl_keyboard* wl_keyboard, u32 serial, struct wl_surface* surface, struct wl_array* keys) {
    (void)data;
    (void)wl_keyboard;
    (void)surface;
    (void)keys;

    WLCLIENT_LOG_TRACE("Keyboard enter: serial=%" PRIu32, serial);
}

/**
* This notifies the client that keyboard focus left one of its surfaces.
*
* This happens:
*   - when the compositor removes keyboard focus from the client surface
*
* Parameters:
*   data        - user-provided pointer passed to wl_keyboard_add_listener
*   wl_keyboard - the wl_keyboard instance
*   serial      - serial identifying the leave event
*   surface     - the wl_surface that lost keyboard focus
*/
static void keyboard_leave(void* data, struct wl_keyboard* wl_keyboard, u32 serial, struct wl_surface* surface) {
    (void)data;
    (void)wl_keyboard;
    (void)surface;

    WLCLIENT_LOG_TRACE("Keyboard leave: serial=%" PRIu32, serial);
}

/**
* This reports a keyboard key press or release.
*
* This happens:
*   - whenever a focused keyboard key changes state
*
* Parameters:
*   data        - user-provided pointer passed to wl_keyboard_add_listener
*   wl_keyboard - the wl_keyboard instance
*   serial      - serial identifying the key event
*   time        - compositor timestamp in milliseconds
*   key         - hardware keycode reported by the compositor
*   state       - wl_keyboard_key_state value for press or release
*/
static void keyboard_key(void* data, struct wl_keyboard* wl_keyboard, u32 serial, u32 time, u32 key, u32 state) {
    (void)data;
    (void)wl_keyboard;

    WLCLIENT_LOG_TRACE(
        "Keyboard key: serial=%" PRIu32 ", time=%" PRIu32 ", key=%" PRIu32 ", state=%" PRIu32,
        serial, time, key, state
    );
}

/**
* This reports the compositor's current keyboard modifier state.
*
* This happens:
*   - whenever depressed, latched, locked, or layout-group state changes
*
* Parameters:
*   data           - user-provided pointer passed to wl_keyboard_add_listener
*   wl_keyboard    - the wl_keyboard instance
*   serial         - serial identifying the modifiers event
*   mods_depressed - bitmask of depressed modifiers
*   mods_latched   - bitmask of latched modifiers
*   mods_locked    - bitmask of locked modifiers
*   group          - active keyboard layout group
*/
static void keyboard_modifiers(void* data, struct wl_keyboard* wl_keyboard, u32 serial, u32 mods_depressed, u32 mods_latched, u32 mods_locked, u32 group) {
    (void)data;
    (void)wl_keyboard;

    WLCLIENT_LOG_TRACE(
        "Keyboard modifiers: serial=%" PRIu32 ", depressed=%" PRIu32 ", latched=%" PRIu32 ", locked=%" PRIu32 ", group=%" PRIu32,
        serial, mods_depressed, mods_latched, mods_locked, group
    );
}

/**
* This reports the compositor's preferred keyboard repeat configuration.
*
* This happens:
*   - after binding a wl_keyboard
*   - whenever the compositor updates key repeat settings
*
* Parameters:
*   data        - user-provided pointer passed to wl_keyboard_add_listener
*   wl_keyboard - the wl_keyboard instance
*   rate        - repeat rate in keys per second, or zero if disabled
*   delay       - repeat delay in milliseconds
*/
static void keyboard_repeat_info(void* data, struct wl_keyboard* wl_keyboard, i32 rate, i32 delay) {
    (void)data;
    (void)wl_keyboard;

    WLCLIENT_LOG_TRACE("Keyboard repeat info: rate=%" PRIi32 ", delay=%" PRIi32, rate, delay);
}
