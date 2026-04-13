#include "wl-util.h"

#include <wayland-client-protocol.h>

void wlclient_hide_surface(struct wl_surface* surface) {
    wl_surface_attach(surface, NULL, 0, 0);
    wl_surface_commit(surface);
}
