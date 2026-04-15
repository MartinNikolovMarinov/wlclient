#pragma once

#include "API.h"
#include "types.h"

//======================================================================================================================
// PUBLIC
//======================================================================================================================

WLCLIENT_API_EXPORT wlclient_error_code wlclient_init(wlclient_allocator* allocator);
WLCLIENT_API_EXPORT void wlclient_shutdown(void);

WLCLIENT_API_EXPORT void wlclient_destroy_window(wlclient_window* window);

//======================================================================================================================
// INTERNALS - these expose internal state that is needed for custom backend development.
//======================================================================================================================

struct wl_display;

WLCLIENT_API_EXPORT struct wl_display* _wlclient_get_wl_display(void);
WLCLIENT_API_EXPORT struct wlclient_global_state* _wlclient_get_wl_global_state(void);

WLCLIENT_API_EXPORT void _wlclient_set_backend_shutdown(void (*shutdown)(void));
WLCLIENT_API_EXPORT void _wlclient_set_backend_destroy_window(void (*destroy_window)(const wlclient_window* window));
WLCLIENT_API_EXPORT void _wlclient_set_backend_resize_window(void (*resize_window)(const wlclient_window* window, i32 framebuffer_width, i32 framebuffer_height));

