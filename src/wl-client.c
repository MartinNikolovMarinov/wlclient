#define _GNU_SOURCE

#include "debug.h"
#include "macro_magic.h"
#include "wl-client.h"
#include "wl-utils.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "xdg-shell-client-protocol.h"
#include "wayland-client-protocol.h"

#include <wayland-client-core.h>
#include <wayland-util.h>

#define ENSURE_OR_GOTO_ERR(expr)                                                    \
do {                                                                                \
    if (!(expr)) {                                                                  \
        _wlclient_report_wayland_fatal(g_state.display, #expr, __FILE__, __LINE__); \
        goto error;                                                                 \
    }                                                                               \
} while(0)

#define WLCLIENT_SHM_REQUIRED_VERSION 1

#define WLCLIENT_MIN_CONTENT_WIDTH 100
#define WLCLIENT_MIN_CONTENT_HEIGHT 100

#define WLCLIENT_MAX_WINDOW_WIDTH 15360
#define WLCLIENT_MAX_WINDOW_HEIGHT 8640

static wlclient_global_state g_state;

//======================================================================================================================
// Helper Declarations
//======================================================================================================================

static void set_allocator(wlclient_allocator* allocator);

static void destroy_all_input_devices(void);
static void destroy_input_device(wlclient_input_device* input_device);
static void destroy_input_device_seat(wlclient_input_device* input_device);
static void destroy_input_device_pointer(wlclient_input_device* input_device);
static void destroy_input_device_keyboard(wlclient_input_device* input_device);
static void store_new_input_device(u32 id, struct wl_seat* seat, u32 seat_version);
static wlclient_input_device* find_input_device_by_seat(struct wl_seat* seat);
static i32 find_input_device_index_by_id(u32 id);
static void remove_and_destroy_input_device_by_id(i32 idx);

static void destroy_window_data(wlclient_window_data* wdata);
static void log_window_data_dimensions(wlclient_window_data* wdata, wlclient_log_level level, const char* function_name);

static bool display_flush(struct wl_display* display);

//======================================================================================================================
// Wayland Handlers
//======================================================================================================================

static void register_global(void* data, struct wl_registry* wl_registry, u32 id, const char* interface, u32 version);
static void register_global_remove(void* data, struct wl_registry* wl_registry, u32 id);

static void shm_pick_format(void *data, struct wl_shm *wl_shm, u32 format);

static void xdg_wm_base_ping(void* data, struct xdg_wm_base* xdg_wm_base, u32 serial);

static void xdg_toplevel_close(void*, struct xdg_toplevel* toplevel);
static void xdg_toplevel_configure(void*, struct xdg_toplevel* toplevel, i32 width, i32 height, struct wl_array* states);
static void xdg_toplevel_configure_bounds(void*, struct xdg_toplevel* toplevel, i32 width, i32 height);
static void xdg_toplevel_configure_wm_capabilities(void*, struct xdg_toplevel* toplevel, struct wl_array* caps);

static void xdg_surface_configure(void* data, struct xdg_surface* xdg_surface, u32 serial);

static void seat_capabilities(void *data, struct wl_seat *wl_seat, u32 capabilities);
static void seat_name(void *data, struct wl_seat *wl_seat, const char *name);

static void surface_preferred_buffer_scale(void* data, struct wl_surface* surface, i32 factor);
static void surface_enter(void *data, struct wl_surface *wl_surface, struct wl_output *output);
static void surface_leave(void *data, struct wl_surface *wl_surface, struct wl_output *output);
static void surface_preferred_buffer_transform(void *data, struct wl_surface *wl_surface, u32 transform);

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

    // Make sure the pixel format is intentionally set to a value.
    g_state.preferred_pixel_format = -1;

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

    ENSURE_OR_GOTO_ERR(g_state.preferred_pixel_format >= 0);

    // Verify device inputs are configured correctly:
    {
        if (g_state.input_devices_count == 0) {
            WLCLIENT_LOG_WARN("No input devices detected");
        }

        for (i32 i = 0; i < g_state.input_devices_count; i++) {
            wlclient_input_device* d = &g_state.input_devices[i];
            ENSURE_OR_GOTO_ERR(d->used);
            ENSURE_OR_GOTO_ERR(d->seat);
            ENSURE_OR_GOTO_ERR(d->seat_id);

            // Technically a mouse and keyboard may have not been plugged in.
            // ENSURE_OR_GOTO_ERR(d->pointer);
            // ENSURE_OR_GOTO_ERR(d->keyboard);
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

    for (i32 i = 0; i < WLCLIENT_WINDOWS_COUNT; i++) {
        if (g_state.windows[i].used) {
            wlclient_window window = { .id = i };
            wlclient_destroy_window(&window);
        }
    }

    if (g_state.backend_shutdown) g_state.backend_shutdown();

    destroy_all_input_devices();

    if (g_state.compositor) wl_compositor_destroy(g_state.compositor);
    if (g_state.subcompositor) wl_subcompositor_destroy(g_state.subcompositor);
    if (g_state.xdgWmBase) xdg_wm_base_destroy(g_state.xdgWmBase);
    if (g_state.shm) wl_shm_destroy(g_state.shm);
    if (g_state.registry) wl_registry_destroy(g_state.registry);

    if (g_state.display) {
        // Flush the pending destory requests before disconnecting.
        bool ok = display_flush(g_state.display);
        WLCLIENT_ASSERT(ok, "Failed to flush on shutdown");
        wl_display_disconnect(g_state.display);
    }

    memset(&g_state, 0, sizeof(g_state));
    g_state.preferred_pixel_format = -1;

    WLCLIENT_LOG_INFO("Shutdown done");
}

wlclient_error_code wlclient_create_window(
    wlclient_window* window,
    u32 content_width, u32 content_height,
    const char* title,
    const wlclient_window_decoration_config* decor_cfg
) {
    i32 ret;
    wlclient_window_data* wdata = NULL;

    WLCLIENT_ASSERT(window, "window argument is null");
    WLCLIENT_ASSERT(title, "title argument is null");

    WLCLIENT_LOG_DEBUG("Creating window (%"PRIi32"x%"PRIi32")", content_width, content_height);

    // Select an unused slot for the new window:
    window->id = -1;
    {
        for (i32 i = 0; i < WLCLIENT_WINDOWS_COUNT; i++) {
            if (!g_state.windows[i].used) {
                wdata = &g_state.windows[i];
                window->id = i;
                break;
            }
        }
        ENSURE_OR_GOTO_ERR(wdata);
    }

    // Set the state that is need in some listener callback
    {
        wdata->decor_logical_height = 0;
        if (decor_cfg && decor_cfg->decor_logical_height > 0) {
            wdata->decor_logical_height = decor_cfg->decor_logical_height;
        }

        wdata->edge_logical_thinkness = 0;
        if (decor_cfg && decor_cfg->edge_logical_thinkness > 0) {
            wdata->edge_logical_thinkness = decor_cfg->edge_logical_thinkness;
        }

        wdata->scale_factor = 1.f;

        wdata->content_logical_width = WLCLIENT_MAX(content_width, WLCLIENT_MIN_CONTENT_WIDTH);
        wdata->content_logical_height = WLCLIENT_MAX(content_height, WLCLIENT_MIN_CONTENT_HEIGHT);

        wdata->window_logical_width = content_width + (2 * wdata->edge_logical_thinkness);
        wdata->window_logical_height = content_height + (2 * wdata->edge_logical_thinkness) + wdata->decor_logical_height;

        wdata->window_max_logical_width = WLCLIENT_MAX_WINDOW_WIDTH;
        wdata->window_max_logical_height = WLCLIENT_MAX_WINDOW_HEIGHT;

        wdata->framebuffer_pixel_width = (u32)wdata->scale_factor * wdata->content_logical_width;
        wdata->framebuffer_pixel_height = (u32)wdata->scale_factor * wdata->content_logical_height;

        wdata->used = true;
        wdata->frame_hide = !(wdata->decor_logical_height > 0);

        // FIXME: set the anon files to -1 here for all nodes
    }

    // Create Surface
    {
        wdata->surface = wl_compositor_create_surface(g_state.compositor);
        ENSURE_OR_GOTO_ERR(wdata->surface);

        const static struct wl_surface_listener surface_listener = {
            .enter = surface_enter,
            .leave = surface_leave,
            .preferred_buffer_scale = surface_preferred_buffer_scale, // sets the scale factor
            .preferred_buffer_transform = surface_preferred_buffer_transform,
        };
        ret = wl_surface_add_listener(wdata->surface, &surface_listener, window);
        ENSURE_OR_GOTO_ERR(ret == 0);
    }

    // Create XDG Surface and TopLevel role.
    {
        wdata->xdg_surface = xdg_wm_base_get_xdg_surface(g_state.xdgWmBase, wdata->surface);
        ENSURE_OR_GOTO_ERR(wdata->xdg_surface);
        wdata->xdg_toplevel = xdg_surface_get_toplevel(wdata->xdg_surface);
        ENSURE_OR_GOTO_ERR(wdata->xdg_toplevel);

        xdg_toplevel_set_title(wdata->xdg_toplevel, title);

        const static struct xdg_surface_listener xdg_surface_listener = {
            .configure = xdg_surface_configure // synchronizes the in flight packets
        };
        ret = xdg_surface_add_listener(wdata->xdg_surface, &xdg_surface_listener, window);
        ENSURE_OR_GOTO_ERR(ret == 0);

        const static struct xdg_toplevel_listener toplevel_listener = {
            .close = xdg_toplevel_close,
            .configure = xdg_toplevel_configure, // sets the in flight window logical width and height
            .configure_bounds = xdg_toplevel_configure_bounds, // sets the in flight window max logical width and height
            .wm_capabilities = xdg_toplevel_configure_wm_capabilities,
        };
        ret = xdg_toplevel_add_listener(wdata->xdg_toplevel, &toplevel_listener, window);
        ENSURE_OR_GOTO_ERR(ret == 0);
    }

    // FIXME: create the edges and the decoration surface and subsurfaces here later

    // Final round trip to finalize window creation.
    wl_surface_commit(wdata->surface);
    ret = wl_display_roundtrip(g_state.display);
    ENSURE_OR_GOTO_ERR(ret >= 0);

    ENSURE_OR_GOTO_ERR(wdata->scale_factor > 0);
    ENSURE_OR_GOTO_ERR(wdata->window_logical_width > 0);
    ENSURE_OR_GOTO_ERR(wdata->window_logical_height > 0);
    ENSURE_OR_GOTO_ERR(wdata->window_max_logical_width > 0);
    ENSURE_OR_GOTO_ERR(wdata->window_max_logical_height > 0);
    ENSURE_OR_GOTO_ERR(wdata->content_logical_width > 0);
    ENSURE_OR_GOTO_ERR(wdata->content_logical_height > 0);
    ENSURE_OR_GOTO_ERR(wdata->framebuffer_pixel_width > 0);
    ENSURE_OR_GOTO_ERR(wdata->framebuffer_pixel_height > 0);

    log_window_data_dimensions(wdata, WLCLIENT_LOG_LEVEL_DEBUG, __func__);

    WLCLIENT_LOG_DEBUG("Window creation done");
    return WLCLIENT_ERROR_OK;

error:
    window->id = -1;
    destroy_window_data(wdata);
    return WLCLIENT_ERROR_WINDOW_CREATE_FAILED;
}

void wlclient_destroy_window(wlclient_window* window) {
    if (!window || window->id < 0) return;

    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);

    WLCLIENT_LOG_DEBUG("Destroying window with id=%" PRIi32 "...", window->id);
    if (g_state.backend_destroy_window) {
        g_state.backend_destroy_window(window);
    }
    destroy_window_data(wdata);
    window->id = -1;
    WLCLIENT_LOG_DEBUG("Destroyed");
}

WLCLIENT_API_EXPORT wlclient_error_code wlclient_poll_events(u64 timeout_ns) {
    WLCLIENT_LOG_TRACE("Polling for events...");

    bool received_event = false;
    enum { DISPLAY_FD }; // TODO: Will probably need an fd for cursor animation.
    struct pollfd pfds[] =
    {
        [DISPLAY_FD] = {.fd = wl_display_get_fd(g_state.display), .events = POLLIN, .revents = 0},
    };
    const i32 pfds_size = WLCLIENT_ARRAY_SIZE(pfds);

    while (!received_event)
    {
        // NOTE: Quote from the manual page:
        //
        // wl_display_prepare_read This function must be called before reading from the file descriptor using
        // wl_display_read_events(). Calling wl_display_prepare_read() announces the calling threads intention to read
        // and ensures that until the thread is ready to read and calls wl_display_read_events(), no other thread will
        // read from the file descriptor. This only succeeds if the event queue is empty though, and if there are
        // undispatched events in the queue, -1 is returned and errno set to EAGAIN.
        while (wl_display_prepare_read(g_state.display) != 0)
        {
            // NOTE: Quote from the manual page:
            //
            // wl_display_dispatch_pending is necessary when a client's main loop wakes up on some fd other than the
            // display fd (network socket, timer fd, etc) and calls wl_display_dispatch_queue() from that callback.
            // This may queue up events in the main queue while reading all data from the display fd.
            //
            // When the main thread returns to the main loop to block, the display fd no longer has data, causing a
            // call to poll(2) (or similar functions) to block indefinitely, even though there are events ready to
            // dispatch.
            //
            // The dispatch is what actually fires the listeners.
            i32 ret = wl_display_dispatch_pending(g_state.display);

            if (ret > 0) {
                // Dispatched at least one already-queued event, so return without blocking.
                received_event = true;
                break;
            }
            if (ret < 0) {
                goto poll_failed;
            }
        }

        if (received_event) goto done_ok;

        bool ok = display_flush(g_state.display);
        if (!ok) {
            wl_display_cancel_read(g_state.display);
            goto poll_failed;
        }

        struct wlclient_poll_result poll_result = wlclient_poll_with_timeout(pfds, pfds_size, timeout_ns);
        if (poll_result.poll_result < 0) {
            wl_display_cancel_read(g_state.display);
            goto poll_failed;
        }
        else if (poll_result.timedout) {
            goto done_ok;
        }

        // NOTE: Quote from the manual page:
        //
        // If a thread successfully calls wl_display_prepare_read(), it must either call wl_display_read_events() when
        // it's ready or cancel the read intention by calling wl_display_cancel_read().
        if (pfds[DISPLAY_FD].revents & POLLIN) {
            // Read and queue events into their corresponding event queues:
            i32 dispatch_events_ret = wl_display_read_events(g_state.display);
            if (dispatch_events_ret < 0) {
                goto poll_failed;
            }

            // Dispatch events in a non-blocking manner. The dispatch is what actually fires the listeners.
            i32 dispatch_pending_ret = wl_display_dispatch_pending(g_state.display);
            if (dispatch_pending_ret > 0) {
                received_event = true;
            }
            else if (dispatch_pending_ret < 0) {
                goto poll_failed;
            }
        }
        else {
            wl_display_cancel_read(g_state.display);
        }
    }

done_ok:
    return WLCLIENT_ERROR_OK;
poll_failed:
    return WLCLIENT_ERROR_EVENT_POLL_FAILED;
}

//======================================================================================================================
// INTERNALS IMPLEMENTATIONS
//======================================================================================================================

struct wl_display* _wlclient_get_wl_display(void) {
    WLCLIENT_ASSERT(g_state.display, "display is null");
    return g_state.display;
}

struct wlclient_global_state* _wlclient_get_wl_global_state(void) {
    return &g_state;
}

wlclient_window_data* _wlclient_get_wl_window_data(const wlclient_window* window) {
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

void _wlclient_set_backend_resize_window(void (*resize_window)(const wlclient_window* window, u32 framebuffer_width, u32 framebuffer_height)) {
    g_state.backend_resize_window = resize_window;
}

//======================================================================================================================
// Helper Implementations
//======================================================================================================================

static void set_allocator(wlclient_allocator* allocator) {
    if (allocator) {
        WLCLIENT_ASSERT(allocator->alloc, "The allocator interface did not set a alloc callback");
        WLCLIENT_ASSERT(allocator->free, "The allocator interface did not set a free callback");
        WLCLIENT_ASSERT(allocator->strdup, "The allocator interface did not set a strdup callback");

        g_state.allocator.alloc = allocator->alloc;
        g_state.allocator.free = allocator->free;
        g_state.allocator.strdup = allocator->strdup;
    }
    else {
        g_state.allocator.alloc = malloc;
        g_state.allocator.free = free;
        g_state.allocator.strdup = strdup;
    }
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
    destroy_input_device_pointer(input_device);
    destroy_input_device_keyboard(input_device);
    destroy_input_device_seat(input_device);

    memset(input_device, 0, sizeof(*input_device));
}

static void destroy_input_device_seat(wlclient_input_device* input_device) {
    if (!input_device) return;

    if (input_device->seat_name) {
        g_state.allocator.free(input_device->seat_name);
    }

    if (input_device->seat) {
        if (input_device->seat_version >= 5)
            wl_seat_release(input_device->seat);
        else
            wl_seat_destroy(input_device->seat);
    }

    input_device->seat = NULL;
    input_device->seat_name = NULL;
}

static void destroy_input_device_pointer(wlclient_input_device* input_device) {
    if (!input_device) return;

    if (input_device->pointer) {
        if (input_device->seat_version >= 3)
            wl_pointer_release(input_device->pointer);
        else
            wl_pointer_destroy(input_device->pointer);
    }

    input_device->pointer = NULL;
}

static void destroy_input_device_keyboard(wlclient_input_device* input_device) {
    if (!input_device) return;

    if (input_device->keyboard) {
        if (input_device->seat_version >= 3)
            wl_keyboard_release(input_device->keyboard);
        else
            wl_keyboard_destroy(input_device->keyboard);
    }

    input_device->keyboard = NULL;
}


static void store_new_input_device(u32 id, struct wl_seat* seat, u32 seat_version) {
    WLCLIENT_PANIC(
        g_state.input_devices_count < WLCLIENT_MAX_INPUT_DEVICES,
        "Maximum allowed seats (%"PRIi32") exceeded.",
        WLCLIENT_MAX_INPUT_DEVICES
    );

    wlclient_input_device new_device = {
        .seat_id = id,
        .seat = seat,
        .seat_version = seat_version,
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

static i32 find_input_device_index_by_id(u32 id) {
    for (i32 i = 0; i < g_state.input_devices_count; i++) {
        if (g_state.input_devices[i].seat_id == id) return i;
    }
    return -1;
}

static void remove_and_destroy_input_device_by_id(i32 idx) {
    WLCLIENT_PANIC(
        idx >= 0 && idx < g_state.input_devices_count,
        "[BUG] Remove failed; invalid index idx=%" PRIi32,
        idx
    );

    wlclient_input_device* input_device = &g_state.input_devices[idx];

    destroy_input_device(input_device);

    if (idx == g_state.input_devices_count - 1) {
        g_state.input_devices_count--;
        return;
    }

    i32 last = g_state.input_devices_count - idx - 1;
    if (last > 0) {
        memmove(
            &g_state.input_devices[idx],
            &g_state.input_devices[idx + 1],
            (usize)(last) * sizeof(*input_device)
        );
    }

    g_state.input_devices_count--;
}

static void destroy_window_data(wlclient_window_data* wdata) {
    if (!wdata) return;

    if (wdata->xdg_toplevel) xdg_toplevel_destroy(wdata->xdg_toplevel);
    if (wdata->xdg_surface) xdg_surface_destroy(wdata->xdg_surface);

    // FIXME: destroy nodes here later
    // destroy_surface_node(&wdata->decoration_node);
    // for (i32 i = 0; i < WLCLIENT_EDGE_COUNT; i++) {
    //     destroy_surface_node(&wdata->edge_nodes[i]);
    // }

    if (wdata->surface) wl_surface_destroy(wdata->surface);

    memset(wdata, 0, sizeof(*wdata));
}

static void log_window_data_dimensions(wlclient_window_data* wdata, wlclient_log_level level, const char* function_name) {
    _wlclient_log_message(
        level, function_name,
        "\n --- Window dimensions ---"
        "\n  scale_factor = %.3f,"
        "\n  window_locical_size = %" PRIu32 "x%" PRIu32 ","
        "\n  window_max_logical_size = %" PRIu32 "x%" PRIu32
        "\n  content_locical_size = %" PRIu32 "x%" PRIu32
        "\n  framebuffer_size = %" PRIu32 "x%" PRIu32,
        (f64) wdata->scale_factor,
        wdata->window_logical_width, wdata->window_logical_height,
        wdata->window_max_logical_width, wdata->window_max_logical_height,
        wdata->content_logical_width, wdata->content_logical_height,
        wdata->framebuffer_pixel_width, wdata->framebuffer_pixel_height
    );
}

static bool display_flush(struct wl_display* display) {
    bool res = true;

    errno = 0;

    while (wl_display_flush(display) < 0) {
        if (errno != EAGAIN) {
            res = false;
            break;
        }

        // Socket send buffer is likely full.
        // Block until the fd becomes writable again
        struct pollfd fd = { .fd = wl_display_get_fd(display), .events = POLLOUT, .revents = 0 };
        while (poll(&fd, 1, -1) < 0) {
            if (errno != EINTR && errno != EAGAIN) {
                res = false;
                break;
            }
        }

        if (!(fd.revents & POLLOUT)) {
            // If POLLOUT is not set, the display connection is no longer writable and should be treated as failed.
            res = false;
            break;
        }
    }

    return res;
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

        static const struct xdg_wm_base_listener listener = {
            .ping = xdg_wm_base_ping
        };
        i32 ret = xdg_wm_base_add_listener(g_state.xdgWmBase, &listener, NULL);
        WLCLIENT_PANIC(ret == 0, "failed to setup xdg base ping listener");
    }
    else if (strcmp(interface, "wl_shm") == 0) {
        WLCLIENT_PANIC(!g_state.shm, "wl_shm re-registered");

        g_state.shm = wl_registry_bind(wl_registry, id, &wl_shm_interface, WLCLIENT_SHM_REQUIRED_VERSION);
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
        WLCLIENT_PANIC(seat, "failed to create wl_seat");

        static const struct wl_seat_listener listener = {
            .capabilities = seat_capabilities,
            .name = seat_name,
        };
        i32 ret = wl_seat_add_listener(seat, &listener, NULL);
        WLCLIENT_PANIC(ret == 0, "failed to add seat listener");

        store_new_input_device(id, seat, effective_version);
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

    {
        i32 idx = find_input_device_index_by_id(id);
        if (idx >= 0) remove_and_destroy_input_device_by_id(idx);
    }

    // TODO: [GLOBAL_OBJECT_REMOVE] Should any other objects be removed? GLFW seems to only handle monitors.
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
        g_state.preferred_pixel_format = (i32) WL_SHM_FORMAT_ARGB8888;
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
* This is the compositor close request for the xdg_toplevel. It indicates that the window should begin shutdown.
*
* This happens:
*   - when the compositor requests that the window be closed
*
* Parameters:
*   data      - user-provided wlclient_window pointer passed to xdg_toplevel_add_listener
*   toplevel  - the xdg_toplevel instance
*/
static void xdg_toplevel_close(void* data, struct xdg_toplevel* toplevel) {
    (void)toplevel;

    wlclient_window* window = data;
    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
    if (wdata->close_handler) {
        wdata->close_handler();
    }
}

/**
* This is the main configure event for the xdg_toplevel. It communicates the compositor's suggested logical size and
* state for the window.
*
* This happens:
*   - after the initial surface commit
*   - whenever the compositor updates the window size or state
*
* Parameters:
*   data      - user-provided wlclient_window pointer passed to xdg_toplevel_add_listener
*   toplevel  - the xdg_toplevel instance
*   width     - suggested logical window width, or 0 if unspecified
*   height    - suggested logical window height, or 0 if unspecified
*   states    - array of xdg_toplevel_state values describing the window state
*/
static void xdg_toplevel_configure(void* data, struct xdg_toplevel* toplevel, i32 width, i32 height, struct wl_array* states) {
    (void)toplevel;
    (void)states;

    WLCLIENT_LOG_DEBUG("Top level configure size = %" PRIi32 "x%" PRIi32 "", width, height);

    wlclient_window* window = data;
    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
    u32 w = width > 0 ? (u32)width : wdata->window_logical_width;
    u32 h = height > 0 ? (u32)height : wdata->window_logical_height;

    wdata->toplevel_config_in_flight_packet.window_logical_width = w;
    wdata->toplevel_config_in_flight_packet.window_logical_height = h;
}

/**
* This reports compositor-provided upper bounds for the xdg_toplevel's logical size.
*
* This happens:
*   - when the compositor provides updated window bounds information
*
* Parameters:
*   data      - user-provided wlclient_window pointer passed to xdg_toplevel_add_listener
*   toplevel  - the xdg_toplevel instance
*   width     - suggested maximum logical window width, or 0 if unspecified
*   height    - suggested maximum logical window height, or 0 if unspecified
*/
static void xdg_toplevel_configure_bounds(void* data, struct xdg_toplevel* toplevel, i32 width, i32 height) {
    (void)toplevel;

    WLCLIENT_LOG_DEBUG("Top level bounds size = %" PRIi32 "x%" PRIi32, width, height);

    wlclient_window* window = data;
    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);
    u32 w = (width > 0) ? (u32) width : WLCLIENT_MAX_WINDOW_WIDTH;
    u32 h = (height > 0) ? (u32) height : WLCLIENT_MAX_WINDOW_HEIGHT;

    wdata->toplevel_config_in_flight_packet.window_max_logical_width = w;
    wdata->toplevel_config_in_flight_packet.window_max_logical_height = h;
}

/**
* This advertises the window management capabilities currently supported for the xdg_toplevel.
*
* This happens:
*   - when the compositor sends updated window management capabilities
*
* Parameters:
*   data      - user-provided wlclient_window pointer passed to xdg_toplevel_add_listener
*   toplevel  - the xdg_toplevel instance
*   caps      - array of xdg_toplevel_wm_capabilities values
*/
static void xdg_toplevel_configure_wm_capabilities(void* data, struct xdg_toplevel* toplevel, struct wl_array* caps) {
    (void)data;
    (void)toplevel;

    WLCLIENT_LOG_TRACE("Toplevel capabilities:");
    u32 *value;
    usize i = 0;
    wl_array_for_each(value, caps) {
        WLCLIENT_LOG_TRACE("  [%" PRIu64 "] = %" PRIu32, i++, *value);
    }
}

/**
* Handles the xdg_surface configure handshake. This is a synchronization point where the client applies any pending
* surface state changes for the configure and then acknowledges the serial. The ack must come after all state changes
* have been applied — it signals the compositor that the client is done processing this configure sequence.
*
* This happens:
*   - after the initial surface commit
*   - whenever the compositor sends a new surface configure event
*
* Parameters:
*   data        - user-provided wlclient_window pointer passed to xdg_surface_add_listener
*   xdg_surface - the xdg_surface instance
*   serial      - serial number that must be acknowledged with xdg_surface_ack_configure
*/
static void xdg_surface_configure(void* data, struct xdg_surface* xdg_surface, u32 serial) {
    WLCLIENT_LOG_DEBUG("Surface configure serial: %" PRIu32, serial);

    wlclient_window* window = data;
    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);

    wdata->window_max_logical_width = wdata->toplevel_config_in_flight_packet.window_max_logical_width;
    wdata->window_max_logical_height = wdata->toplevel_config_in_flight_packet.window_max_logical_height;

    wdata->window_logical_width = WLCLIENT_MIN(
        wdata->toplevel_config_in_flight_packet.window_logical_width,
        wdata->window_max_logical_width
    );
    wdata->window_logical_height = WLCLIENT_MIN(
        wdata->toplevel_config_in_flight_packet.window_logical_height,
        wdata->window_max_logical_height
    );

    wdata->content_logical_width = wdata->window_logical_width - (2 * wdata->edge_logical_thinkness);
    wdata->content_logical_height = wdata->window_logical_height - (2 * wdata->edge_logical_thinkness) - wdata->decor_logical_height;

    // TODO: [FRACTIONAL_SCALING] f32 rounding
    wdata->framebuffer_pixel_width = (u32)wdata->scale_factor * wdata->content_logical_width;
    wdata->framebuffer_pixel_height = (u32)wdata->scale_factor * wdata->content_logical_height;

    // Notify the backend for a resize event:
    if (g_state.backend_resize_window && wdata->used) {
        g_state.backend_resize_window(window, wdata->framebuffer_pixel_width, wdata->framebuffer_pixel_height);
    }

    log_window_data_dimensions(wdata, WLCLIENT_LOG_LEVEL_DEBUG, __func__);

    // Acknowledge the current configuration handshake with the compositor.
    xdg_surface_ack_configure(xdg_surface, serial);

    // FIXME: This is where I need to set these for edges and decoration.
    // wl_surface_set_buffer_scale ..
    // wl_surface_attach ..
    // update state
    // wl_surface_commit ..

    // zero out the in-flight packet/
    memset(&wdata->toplevel_config_in_flight_packet, 0, sizeof(wdata->toplevel_config_in_flight_packet));
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
    input_device->capabilities = capabilities;

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
            destroy_input_device_keyboard(input_device);
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
            destroy_input_device_pointer(input_device);
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
    if (input_device->seat_name) {
        g_state.allocator.free(input_device->seat_name);
    }
    input_device->seat_name = g_state.allocator.strdup(name);

    WLCLIENT_PANIC(input_device->seat_name, "Failed to allocate memory for the seat name");
}

/**
* This reports the compositor's preferred buffer scale for the surface. The client should render buffers at this
* scale and advertise it back with wl_surface_set_buffer_scale.
*
* This happens:
*   - after the surface becomes associated with outputs
*   - whenever the compositor's preferred buffer scale changes
*
* Parameters:
*   data    - user-provided wlclient_window pointer passed to wl_surface_add_listener
*   surface - the wl_surface instance
*   factor  - preferred buffer scale for the surface
*/
static void surface_preferred_buffer_scale(void* data, struct wl_surface* surface, i32 factor) {
    wlclient_window* window = data;
    wlclient_window_data* wdata = _wlclient_get_wl_window_data(window);

    WLCLIENT_LOG_DEBUG("Preferred buffer scale: %" PRIi32, factor);

    if ((i32)wdata->scale_factor == factor) return;

    wdata->scale_factor = (f32)factor;
    wl_surface_set_buffer_scale(surface, (i32) wdata->scale_factor); // TODO: [FRACTIONAL_SCALING] f32 rounding

    // FIXME: what else needs to be notified here ? I guess the backend resize should be triggered.
}

/**
* This notifies the client that the surface has entered the region of an output.
*
* This happens:
*   - when any part of the surface first becomes visible on a given wl_output
*   - when the surface is moved or resized so that it overlaps a new wl_output
*
* Parameters:
*   data       - user-provided pointer passed to wl_surface_add_listener
*   wl_surface - the wl_surface instance
*   output     - the wl_output the surface has entered
*/
static void surface_enter(void *data, struct wl_surface *wl_surface, struct wl_output *output) {
    (void)data;
    (void)wl_surface;
    (void)output;

    WLCLIENT_LOG_DEBUG("Surface enter");
}

/**
* This notifies the client that the surface has left the region of an output.
*
* This happens:
*   - when the surface no longer overlaps a wl_output it previously entered
*   - when the output is unplugged or otherwise removed
*
* Parameters:
*   data       - user-provided pointer passed to wl_surface_add_listener
*   wl_surface - the wl_surface instance
*   output     - the wl_output the surface has left
*/
static void surface_leave(void *data, struct wl_surface *wl_surface, struct wl_output *output) {
    (void)data;
    (void)wl_surface;
    (void)output;

    WLCLIENT_LOG_DEBUG("Surface leave");
}

/**
* This advertises the buffer transform the compositor prefers for this surface.
*
* This happens:
*   - when the compositor determines an optimal wl_output_transform for attached buffers
*   - when that preferred transform changes (e.g. output rotation)
*
* Parameters:
*   data       - user-provided pointer passed to wl_surface_add_listener
*   wl_surface - the wl_surface instance
*   transform  - wl_output_transform value the compositor prefers for buffers
*/
static void surface_preferred_buffer_transform(void *data, struct wl_surface *wl_surface, u32 transform) {
    (void)data;
    (void)wl_surface;
    (void)transform;

    WLCLIENT_LOG_DEBUG("Surface preferred buffer transformaiton");
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
