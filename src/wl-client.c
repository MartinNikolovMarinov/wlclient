#define _GNU_SOURCE

#include "wl-client.h"
#include "debug.h"
#include "macro_magic.h"

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

//======================================================================================================================
// Wayland Handlers
//======================================================================================================================

static void register_global(void* data, struct wl_registry* wl_registry, u32 id, const char* interface, u32 version);
static void register_global_remove(void* data, struct wl_registry* wl_registry, u32 id);

static void shm_pick_format(void *data, struct wl_shm *wl_shm, u32 format);

static void xdg_wm_base_ping(void* data, struct xdg_wm_base* xdg_wm_base, u32 serial);

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

        const static struct wl_registry_listener registry_listener = {
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

    for (i32 i = 0; i < g_state.input_device_count; i++) {
        struct wlclient_input_device* input_device = &g_state.input_device[i];
        destroy_input_device(input_device);
    }

    g_state.input_device_count = 0;

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
    }
    else if (strcmp(interface, "wl_subcompositor") == 0) {
        WLCLIENT_PANIC(!g_state.subcompositor, "wl_subcompositor re-registered");
        u32 effective_version = WLCLIENT_MIN((u32) wl_subcompositor_interface.version, version);
        g_state.subcompositor = wl_registry_bind(wl_registry, id, &wl_subcompositor_interface, effective_version);
    }
    else if (strcmp(interface, "xdg_wm_base") == 0) {
        WLCLIENT_PANIC(!g_state.xdgWmBase, "xdg_wm_base re-registered");
        u32 effective_version = WLCLIENT_MIN((u32) xdg_wm_base_interface.version, version);
        g_state.xdgWmBase = wl_registry_bind(wl_registry, id, &xdg_wm_base_interface, effective_version);
        if (!g_state.xdgWmBase) return;

        // Setup ping handler
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
        if (!g_state.shm) return;

        static const struct wl_shm_listener listener = {
            .format = shm_pick_format
        };
        i32 ret = wl_shm_add_listener(g_state.shm, &listener, NULL);
        WLCLIENT_PANIC(ret == 0, "failed to setup shm format listener");
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
    WLCLIENT_LOG_TRACE("Removing %" PRIu32, id);
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
