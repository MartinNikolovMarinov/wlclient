#pragma once

#include "API.h"
#include "types.h"

WLCLIENT_API_EXPORT wlclient_error_code wlclient_init(void);
WLCLIENT_API_EXPORT void wlclient_shutdown(void);

WLCLIENT_API_EXPORT wlclient_error_code wlclient_create_window(i32 width, i32 height, const char* title, i32 decor_height, wlclient_window* window);
WLCLIENT_API_EXPORT void wlclient_destroy_window(wlclient_window* window);
WLCLIENT_API_EXPORT void wlclient_get_window_size(const wlclient_window* window, i32* width, i32* height);
WLCLIENT_API_EXPORT void wlclient_get_framebuffer_size(const wlclient_window* window, i32* width, i32* height);

WLCLIENT_API_INTERNAL struct wl_display* wlclient_get_wl_display(void);
WLCLIENT_API_INTERNAL wlclient_window_data* wlclient_get_wl_window_data(const wlclient_window* window);

WLCLIENT_API_INTERNAL void wlclient_set_backend_shutdown(void (*shutdown)(void));
WLCLIENT_API_INTERNAL void wlclient_set_backend_destroy_window(void (*destroy_window)(const wlclient_window* window));
WLCLIENT_API_INTERNAL void wlclient_set_backend_resize_window(void (*resize_window)(const wlclient_window* window, i32 framebuffer_width, i32 framebuffer_height));
