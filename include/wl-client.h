#pragma once

#include "API.h"
#include "types.h"

typedef struct wlclient_window {
    i32 id;
} wlclient_window;

WLCLIENT_API_EXPORT wlclient_error_code wlclient_init(void);
WLCLIENT_API_EXPORT void wlclient_shutdown(void);

WLCLIENT_API_EXPORT wlclient_error_code wlclient_create_window(i32 width, i32 height, const char* title, wlclient_window* window);
WLCLIENT_API_EXPORT void wlclient_destroy_window(wlclient_window* window);
