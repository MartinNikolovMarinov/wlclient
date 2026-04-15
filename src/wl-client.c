#include "wl-client.h"
#include "debug.h"
#include "macro_magic.h"

#include <string.h>

#include "xdg-shell-client-protocol.h"
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-util.h>

#define ENSURE_OR_GOTO_ERR(expr)                                               \
do {                                                                           \
    if (!(expr)) {                                                             \
        _wlclient_report_wayland_fatal(g_display, #expr, __FILE__, __LINE__);  \
        goto error;                                                            \
    }                                                                          \
} while(0)

#define WLCLIENT_MAX_SEATS 5

typedef struct wlclient_input_state {
    bool used;
    u32 seat_id;
    u32 capabilities;
    struct wl_seat* seat;
    char* seat_name;
    struct wl_pointer* pointer;
    struct wl_keyboard* keyboard;
} wlclient_seat_state;

// A wl_display object represents a client connection to a Wayland compositor.
static struct wl_display* g_display = NULL;
// The registry object is the compositor’s global object list. Exposes all global interfaces provided by the compositor.
static struct wl_registry* g_registry = NULL;
// The XDG WM base is the entry point for creating desktop-style windows in Wayland.
static struct xdg_wm_base* g_xdgWmBase = NULL;

// The compositor is the factory interface for creating surfaces and regions.
static struct wl_compositor* g_compositor = NULL;
// The global interface exposing sub-surface compositing capabilities. This is needed for the decoration subsurface.
static struct wl_subcompositor* g_subcompositor = NULL;

// A singleton global object that provides support for shared memory.
static struct wl_shm* g_shm = NULL;
// The preferred pixel format for software rendering.
static i32 g_preffered_pixel_format = -1;

// State for all the open windows.
static wlclient_window_data g_windows[WLCLIENT_WINDOWS_COUNT] = {0};

// State for input devices.
static struct wlclient_input_state g_input_state[WLCLIENT_MAX_SEATS] = {0};
i32 g_input_state_count = 0;

// Backend hooks:
static void (*wlclient_backend_shutdown)(void);
static void (*wlclient_backend_destroy_window)(const wlclient_window* window);
static void (*wlclient_backend_resize_window)(const wlclient_window* window, i32 framebuffer_width, i32 framebuffer_height);

//======================================================================================================================
// Helper Declarations
//======================================================================================================================

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

wlclient_error_code wlclient_init(void) {
    WLCLIENT_LOG_INFO("Initializing...");

    i32 ret = 0;

    g_display = wl_display_connect(NULL);
    if (!g_display) {
        WLCLIENT_LOG_FATAL("Failed to connect to display");
        return WLCLIENT_ERROR_INIT_FAILED;
    }

    // Setup the global registry
    {
        g_registry = wl_display_get_registry(g_display);
        ENSURE_OR_GOTO_ERR(g_registry);

        const static struct wl_registry_listener registry_listener = {
            .global = register_global,
            .global_remove = register_global_remove,
        };
        ret = wl_registry_add_listener(g_registry, &registry_listener, NULL);
        ENSURE_OR_GOTO_ERR(ret == 0);

        // Block until all pending request to discover globals are processed.
        ret = wl_display_roundtrip(g_display);
        ENSURE_OR_GOTO_ERR(ret >= 0);
    }

    // Verify all necessary globals are registered.
    ENSURE_OR_GOTO_ERR(g_compositor);
    ENSURE_OR_GOTO_ERR(g_subcompositor);
    ENSURE_OR_GOTO_ERR(g_xdgWmBase);
    ENSURE_OR_GOTO_ERR(g_shm);

    WLCLIENT_LOG_INFO("Initialization done");
    return WLCLIENT_ERROR_OK;

error:
    wlclient_shutdown();
    return WLCLIENT_ERROR_INIT_FAILED;
}

void wlclient_shutdown(void) {
    WLCLIENT_LOG_INFO("Shutting down...");

    // for (i32 i = 0; i < WLCLIENT_WINDOWS_COUNT; i++) {
    //     if (g_windows[i].used) {
    //         wlclient_window window = { .id = i };
    //         wlclient_destroy_window(&window);
    //     }
    // }

    wlclient_backend_destroy_window = NULL;
    wlclient_backend_resize_window = NULL;

    if (wlclient_backend_shutdown) {
        wlclient_backend_shutdown();
        wlclient_backend_shutdown = NULL;
    }

    // destroy_all_input_devices();

    if (g_compositor) wl_compositor_destroy(g_compositor);
    if (g_subcompositor) wl_subcompositor_destroy(g_subcompositor);
    if (g_xdgWmBase) xdg_wm_base_destroy(g_xdgWmBase);
    if (g_shm) wl_shm_destroy(g_shm);
    if (g_registry) wl_registry_destroy(g_registry);
    if (g_display) wl_display_disconnect(g_display);

    g_compositor = NULL;
    g_subcompositor = NULL;
    g_xdgWmBase = NULL;
    g_shm = NULL;
    g_preffered_pixel_format = -1;
    g_registry = NULL;
    g_display = NULL;

    WLCLIENT_LOG_INFO("Shutdown done");
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
        WLCLIENT_PANIC(!g_compositor, "wl_compositor re-registered");
        u32 effective_version = WLCLIENT_MIN((u32) wl_compositor_interface.version, version);
        g_compositor = wl_registry_bind(wl_registry, id, &wl_compositor_interface, effective_version);
    }
    else if (strcmp(interface, "wl_subcompositor") == 0) {
        WLCLIENT_PANIC(!g_subcompositor, "wl_subcompositor re-registered");
        u32 effective_version = WLCLIENT_MIN((u32) wl_subcompositor_interface.version, version);
        g_subcompositor = wl_registry_bind(wl_registry, id, &wl_subcompositor_interface, effective_version);
    }
    else if (strcmp(interface, "xdg_wm_base") == 0) {
        WLCLIENT_PANIC(!g_xdgWmBase, "xdg_wm_base re-registered");
        u32 effective_version = WLCLIENT_MIN((u32) xdg_wm_base_interface.version, version);
        g_xdgWmBase = wl_registry_bind(wl_registry, id, &xdg_wm_base_interface, effective_version);
        if (!g_xdgWmBase) return;

        // Setup ping handler
        static const struct xdg_wm_base_listener listender = {
            .ping = xdg_wm_base_ping
        };
        i32 ret = xdg_wm_base_add_listener(g_xdgWmBase, &listender, NULL);
        WLCLIENT_PANIC(ret == 0, "failed to setup xdg base ping listener");
    }
    else if (strcmp(interface, "wl_shm") == 0) {
        WLCLIENT_PANIC(!g_shm, "wl_shm re-registered");

        u32 effective_version = WLCLIENT_MIN((u32) wl_shm_interface.version, version);
        g_shm = wl_registry_bind(wl_registry, id, &wl_shm_interface, effective_version);
        if (!g_shm) return;

        static const struct wl_shm_listener listener = {
            .format = shm_pick_format
        };
        i32 ret = wl_shm_add_listener(g_shm, &listener, NULL);
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
        g_preffered_pixel_format = (i32) WL_SHM_FORMAT_ARGB8888;
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
