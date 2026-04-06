#include "debug.h"
#include "macro_magic.h"
#include "types.h"
#include "wl-client.h"
#include "xdg-shell-client-protocol.h"

#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include <wayland-util.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

// A wl_display object represents a client connection to a Wayland compositor.
static struct wl_display* g_display = NULL;
// The registry object is the compositor’s global object list. Exposes all global interfaces provided by the compositor.
static struct wl_registry* g_registry = NULL;
// The XDG WM base is the entry point for creating desktop-style windows in Wayland.
static struct xdg_wm_base* g_xdgWmBase = NULL;

// The compositor is the factory interface for creating surfaces and regions.
static struct wl_compositor* g_compositor = NULL;

// TODO: Make this dynamic memory:
#define WLCLIENT_WINDOWS_COUNT 5

typedef struct wlclient_window_data {
    bool used;
    i32 width, height;
    i32 max_width, max_height;
    // Surface is the raw drawable object. It represents a rectangular area to which to can attach pixel buffers.
    struct wl_surface* surface;
    // XDG Surface makes the raw surface participate in window management. Adds lifecycle sync (configure/ack).
    struct xdg_surface* xdg_surface;
    // XDG Top level turns the surface into a real window. Adds window behavior.
    struct xdg_toplevel* xdg_toplevel;
} wlclient_window_data;

static wlclient_window_data g_windows[WLCLIENT_WINDOWS_COUNT] = {0};

static void destroy_window_data(wlclient_window_data* window_data);

static void register_global(void* data, struct wl_registry* wl_registry, u32 name, const char* interface, u32 version);
static void register_global_remove(void* data, struct wl_registry* wl_registry, u32 name);
static void xdg_wm_base_ping(void* data, struct xdg_wm_base* xdg_wm_base, u32 serial);
static void xdg_surface_configure(void* data, struct xdg_surface* xdg_surface, uint32_t serial);

static void xdg_toplevel_close(void*, struct xdg_toplevel* toplevel);
static void xdg_toplevel_configure(void*, struct xdg_toplevel* toplevel, i32 width, i32 height, struct wl_array* states);
static void xdg_toplevel_configure_bounds(void*, struct xdg_toplevel* toplevel, i32 width, i32 height);
static void xdg_toplevel_configure_wm_capabilities(void*, struct xdg_toplevel* toplevel, struct wl_array* caps);

wlclient_error_code wlclient_init(void) {
    WLCLIENT_LOG_DEBUG("Initializing...");

    i32 ret = 0;

    g_display = wl_display_connect(NULL);
    if (!g_display) goto error;

    // Setup the registry
    {
        g_registry = wl_display_get_registry(g_display);
        if (!g_registry) goto error;

        // The registry listener is a callback table for the global object registry.
        const struct wl_registry_listener registry_listener = {
            .global = register_global,
            .global_remove = register_global_remove,
        };
        ret = wl_registry_add_listener(g_registry, &registry_listener, NULL);
        if (ret != 0) goto error;

        // Block until all pending request are processed
        ret = wl_display_roundtrip(g_display);
        if (ret < 0) goto error;

        WLCLIENT_LOG_DEBUG("Registry initialized");
    }

    if (!g_compositor) goto error;
    if (!g_xdgWmBase) goto error;
    // Panic(g_shm, "Compositor did not advertise wl_shm");
    // Panic(g_seat, "Compositor did not advertise wl_seat");
    // Panic(g_pixelFormat == PixelFormat::UNDEFINED, "Did not find supported pixel format");

    WLCLIENT_LOG_DEBUG("Initialization done");
    return WLCLIENT_OK;

error:
    wlclient_shutdown();
    return WLCLIENT_ERROR_INIT_FAILED;
}

void wlclient_shutdown(void) {
    WLCLIENT_LOG_DEBUG("Shuttingdown...");

    if (g_compositor) wl_compositor_destroy(g_compositor);
    if (g_xdgWmBase) xdg_wm_base_destroy(g_xdgWmBase);
    if (g_registry) wl_registry_destroy(g_registry);
    if (g_display) wl_display_disconnect(g_display);

    g_registry = NULL;
    g_display = NULL;
    g_xdgWmBase = NULL;
    g_compositor = NULL;

    WLCLIENT_LOG_DEBUG("Shutdown");
}

wlclient_error_code wlclient_create_window(i32 width, i32 height, const char* title, wlclient_window* window) {
    i32 ret;
    wlclient_window_data* wdata = NULL;

    if (!window) goto error;
    if (!title) goto error;

    WLCLIENT_LOG_DEBUG("Creating window -- width=%" PRIi32 ", height=%" PRIi32 ", title=%s", width, height, title);

    window->id = -1;
    for (i32 i = 0; i < WLCLIENT_WINDOWS_COUNT; i++) {
        if (!g_windows[i].used) {
            wdata = &g_windows[i];
            window->id = i;
            break;
        }
    }
    if (!wdata) {
        WLCLIENT_LOG_ERR("No more unused windows. Max allowed windows are %" PRIi32, WLCLIENT_WINDOWS_COUNT);
        goto error;
    }

    wdata->surface = wl_compositor_create_surface(g_compositor);
    if (!wdata->surface) goto error;
    wdata->xdg_surface = xdg_wm_base_get_xdg_surface(g_xdgWmBase, wdata->surface);
    if (!wdata->xdg_surface) goto error;
    wdata->xdg_toplevel = xdg_surface_get_toplevel(wdata->xdg_surface);
    if (!wdata->xdg_toplevel) goto error;

    xdg_toplevel_set_title(wdata->xdg_toplevel, title);

    struct xdg_surface_listener surface_listener = {0};
    surface_listener.configure = xdg_surface_configure;
    ret = xdg_surface_add_listener(wdata->xdg_surface, &surface_listener, wdata);
    if (ret != 0) goto error;

    struct xdg_toplevel_listener toplevel_listener = {0};
    toplevel_listener.close = xdg_toplevel_close;
    toplevel_listener.configure = xdg_toplevel_configure;
    toplevel_listener.configure_bounds = xdg_toplevel_configure_bounds;
    toplevel_listener.wm_capabilities = xdg_toplevel_configure_wm_capabilities;
    ret = xdg_toplevel_add_listener(wdata->xdg_toplevel, &toplevel_listener, wdata);
    if (ret != 0) goto error;

    wdata->height = height;
    wdata->width = width;

    xdg_surface_set_window_geometry(wdata->xdg_surface, 0, 0, wdata->height, wdata->width);

    // Sends all previously queued changes to the compositor and wait for the roundtrip:
    wl_surface_commit(wdata->surface);
    ret = wl_display_roundtrip(g_display);
    if (ret < 0) goto error;

    wdata->used = true;

    WLCLIENT_LOG_DEBUG("Created successfully");
    return WLCLIENT_OK;

error:
    destroy_window_data(wdata);
    return WLCLIENT_ERROR_WINDOW_CREATE_FAILED;
}

void wlclient_destroy_window(wlclient_window* window) {
    if (!window || window->id < 0 || window->id >= WLCLIENT_WINDOWS_COUNT) return;
    wlclient_window_data* wdata = &g_windows[window->id];
    if (!wdata->used) return;

    WLCLIENT_LOG_DEBUG("Destroying window with id=%" PRIi32 "...", window->id);
    destroy_window_data(wdata);
    window->id = -1;
    WLCLIENT_LOG_DEBUG("Destroyed");
}

static void destroy_window_data(wlclient_window_data* window_data) {
    if (!window_data) return;

    if (window_data->xdg_toplevel) xdg_toplevel_destroy(window_data->xdg_toplevel);
    if (window_data->xdg_surface) xdg_surface_destroy(window_data->xdg_surface);
    if (window_data->surface) wl_surface_destroy(window_data->surface);

    memset(window_data, 0, sizeof(*window_data));
}

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
*   name      - unique numeric identifier for this global
*   interface - string name of the interface (e.g. "wl_compositor", "wl_seat")
*   version   - maximum supported version of the interface
*/
static void register_global(void* data, struct wl_registry* wl_registry, u32 name, const char* interface, u32 version) {
    (void)data;

    if (strcmp(interface, "wl_compositor") == 0) {
        u32 effective_version = WLCLIENT_MIN((u32) wl_compositor_interface.version, version);
        g_compositor = wl_registry_bind(wl_registry, name, &wl_compositor_interface, effective_version);
    }
    else if (strcmp(interface, "xdg_wm_base") == 0) {
        u32 effective_version = WLCLIENT_MIN((u32) xdg_wm_base_interface.version, version);
        g_xdgWmBase = wl_registry_bind(wl_registry, name, &xdg_wm_base_interface, effective_version);
        if (!g_xdgWmBase) return;

        // Setup ping handler
        struct xdg_wm_base_listener listender = {0};
        listender.ping = xdg_wm_base_ping;
        i32 ret = xdg_wm_base_add_listener(g_xdgWmBase, &listender, NULL);
        (void) ret; // FIXME: Panic if this fails!
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
*   name      - unique numeric identifier for the removed global
*/
static void register_global_remove(void* data, struct wl_registry* wl_registry, u32 name) {
    (void)data;
    (void)wl_registry;
    WLCLIENT_LOG_TRACE("Removing %" PRIu32, name);
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
* This is the configure handshake for the xdg_surface. The client must acknowledge the serial before presenting
* content for that configure.
*
* This happens:
*   - after the initial surface commit
*   - whenever the compositor sends a new surface configure event
*
* Parameters:
*   data        - user-provided pointer passed to xdg_surface_add_listener
*   xdg_surface - the xdg_surface instance
*   serial      - serial number that must be acknowledged with xdg_surface_ack_configure
*/
static void xdg_surface_configure(void* data, struct xdg_surface* xdg_surface, uint32_t serial) {
    (void)data;

    WLCLIENT_LOG_TRACE("Surface configure serial: %" PRIu32, serial);
    xdg_surface_ack_configure(xdg_surface, serial);
}

/**
* This is the compositor close request for the xdg_toplevel. It indicates that the window should begin shutdown.
*
* This happens:
*   - when the compositor requests that the window be closed
*
* Parameters:
*   data      - user-provided pointer passed to xdg_toplevel_add_listener
*   toplevel  - the xdg_toplevel instance
*/
static void xdg_toplevel_close(void* data, struct xdg_toplevel* toplevel) {
    (void)data;
    (void)toplevel;

    WLCLIENT_LOG_TRACE("Toplevel close");
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
*   data      - user-provided pointer passed to xdg_toplevel_add_listener
*   toplevel  - the xdg_toplevel instance
*   width     - suggested logical window width, or 0 if unspecified
*   height    - suggested logical window height, or 0 if unspecified
*   states    - array of xdg_toplevel_state values describing the window state
*/
static void xdg_toplevel_configure(void* data, struct xdg_toplevel* toplevel, i32 width, i32 height, struct wl_array* states) {
    (void)toplevel;
    (void)states;

    WLCLIENT_LOG_TRACE("Toplevel configure: size %" PRIi32 "x%" PRIi32, width, height);

    wlclient_window_data* wdata = data;
    if (width > 0) wdata->width = width;
    if (height > 0) wdata->height = height;
}

/**
* This reports compositor-provided upper bounds for the xdg_toplevel's logical size.
*
* This happens:
*   - when the compositor provides updated window bounds information
*
* Parameters:
*   data      - user-provided pointer passed to xdg_toplevel_add_listener
*   toplevel  - the xdg_toplevel instance
*   width     - suggested maximum logical window width, or 0 if unspecified
*   height    - suggested maximum logical window height, or 0 if unspecified
*/
static void xdg_toplevel_configure_bounds(void* data, struct xdg_toplevel* toplevel, i32 width, i32 height) {
    (void)data;
    (void)toplevel;

    WLCLIENT_LOG_TRACE("Toplevel bounds: size %" PRIi32 "x%" PRIi32, width, height);

    wlclient_window_data* wdata = data;
    if (width > 0) wdata->max_width = width;
    if (height > 0) wdata->max_height = height;
}

/**
* This advertises the window management capabilities currently supported for the xdg_toplevel.
*
* This happens:
*   - when the compositor sends updated window management capabilities
*
* Parameters:
*   data      - user-provided pointer passed to xdg_toplevel_add_listener
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
