#include "debug.h"
#include "macro_magic.h"
#include "types.h"
#include "wl-client.h"
#include "xdg-shell-client-protocol.h"

#include <errno.h>
#include <string.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

// TODO: The errno value can be missleading if used for wayland function calls that don't set it.
//       I need to be careful with that.
#define WL_TRY(expr, error_code)                                                  \
    do {                                                                          \
        if (!(expr)) {                                                            \
            i32 WLCLIENT_CONCAT(saved_errno_, __LINE__) = errno;                  \
            if (WLCLIENT_CONCAT(saved_errno_, __LINE__) != 0) {                   \
                WLCLIENT_LOG_ERR(                                                 \
                    "expr=(" WLCLIENT_STRINGIFY(expr) ") failed; errno = %d (%s)",\
                    WLCLIENT_CONCAT(saved_errno_, __LINE__),                      \
                    strerror(WLCLIENT_CONCAT(saved_errno_, __LINE__))             \
                );                                                                \
            }                                                                     \
            else {                                                                \
                WLCLIENT_LOG_ERR("expr=(" WLCLIENT_STRINGIFY(expr) ") failed");   \
            }                                                                     \
            return (error_code);                                                  \
        }                                                                         \
    } while (0)

// A wl_display object represents a client connection to a Wayland compositor.
static struct wl_display* g_display = NULL;
// The registry object is the compositor’s global object list. Exposes all global interfaces provided by the compositor.
static struct wl_registry* g_registry = NULL;
// The XDG WM base is the entry point for creating desktop-style windows in Wayland.
static struct xdg_wm_base* g_xdgWmBase = NULL;

// The compositor is the factory interface for creating surfaces and regions.
static struct wl_compositor *g_compositor = NULL;

static void register_global(void *data, struct wl_registry *wl_registry, u32 name, const char *interface, u32 version);
static void register_global_remove(void *data, struct wl_registry *wl_registry, u32 name);
static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, u32 serial);

wlclient_error_code wlclient_init(void) {
    WLCLIENT_LOG_DEBUG("Initializing...");

    g_display = wl_display_connect(NULL);
    WL_TRY(g_display, WLCLIENT_ERROR_INIT_FAILED);

    // Setup the registry
    {
        g_registry = wl_display_get_registry(g_display);
        WL_TRY(g_registry, WLCLIENT_ERROR_INIT_FAILED);

        // The registry listener is a callback table for the global object registry.
        const struct wl_registry_listener registry_listener = {
            .global = register_global,
            .global_remove = register_global_remove,
        };
        i32 ret = wl_registry_add_listener(g_registry, &registry_listener, NULL);
        WL_TRY(ret == 0, WLCLIENT_ERROR_INIT_FAILED);

        // Block until all pending request are processed
        i32 ret2 = wl_display_roundtrip(g_display);
        WL_TRY(ret2 >= 0, WLCLIENT_ERROR_INIT_FAILED);

        WLCLIENT_LOG_DEBUG("Registry initialized");
    }

    WL_TRY(g_compositor, WLCLIENT_ERROR_INIT_FAILED);
    WL_TRY(g_xdgWmBase, WLCLIENT_ERROR_INIT_FAILED);
    // Panic(g_shm, "Compositor did not advertise wl_shm");
    // Panic(g_seat, "Compositor did not advertise wl_seat");
    // Panic(g_pixelFormat == PixelFormat::UNDEFINED, "Did not find supported pixel format");

    WLCLIENT_LOG_DEBUG("Initialization done");
    return WLCLIENT_OK;
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

static void register_global(void *data, struct wl_registry *wl_registry, u32 name, const char *interface, u32 version) {
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

static void register_global_remove(void *data, struct wl_registry *wl_registry, u32 name) {
    (void)data;
    (void)wl_registry;
    WLCLIENT_LOG_TRACE("Removing %d", name);
}

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, u32 serial) {
    (void)data;

    WLCLIENT_LOG_TRACE("Ping received serial: %d", serial);
    xdg_wm_base_pong(xdg_wm_base, serial);
}
