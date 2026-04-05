#include "debug.h"
#include "wl-client.h"
#include "types.h"

#include <errno.h> // IWYU pragma: keep
#include <string.h>
#include <wayland-client-core.h>

// A wl_display object represents a client connection to a Wayland compositor.
struct wl_display* g_display = NULL;

wlclient_error_code wlclient_init(void) {
    WLCLIENT_LOG_DEBUG("Initializing...");

    g_display = wl_display_connect(NULL);
    WLCLIENT_RETURN_ERRNO_IF(!g_display, WLCLIENT_ERROR_CONNECT_FAILED);

    WLCLIENT_LOG_DEBUG("Initialization done");
    return WLCLIENT_OK;
}

void wlclient_shutdown(void) {
    WLCLIENT_LOG_DEBUG("Shuttingdown...");
    wl_display_disconnect(g_display);
    WLCLIENT_LOG_DEBUG("Shutdown");
}
