#pragma once

#include "API.h"
#include "types.h"

//======================================================================================================================
// PUBLIC
//======================================================================================================================

WLCLIENT_API_EXPORT wlclient_error_code wlclient_init(wlclient_allocator* allocator);
WLCLIENT_API_EXPORT void wlclient_shutdown(void);

WLCLIENT_API_EXPORT wlclient_error_code wlclient_create_window(
    wlclient_window* window,
    u32 content_width, u32 content_height,
    const char* title,
    const wlclient_window_decoration_config* decor_cfg
);
WLCLIENT_API_EXPORT void wlclient_destroy_window(wlclient_window* window);
WLCLIENT_API_EXPORT void wlclient_get_framebuffer(wlclient_window* window, u32* w, u32* h);
WLCLIENT_API_EXPORT wlclient_error_code wlclient_toggle_window_decor(wlclient_window* window);

WLCLIENT_API_EXPORT wlclient_error_code wlclient_poll_events(u64 timeout_ns);

WLCLIENT_API_EXPORT void wlclient_set_close_handler(wlclient_window* window, wlclient_close_handler handler);
WLCLIENT_API_EXPORT void wlclient_set_size_change_handler(wlclient_window* window, wlclient_size_change_handler handler);
WLCLIENT_API_EXPORT void wlclient_set_framebuffer_change_handler(wlclient_window* window, wlclient_framebuffer_change_handler handler);
WLCLIENT_API_EXPORT void wlclient_set_scale_factor_change_handler(wlclient_window* window, wlclient_scale_factor_change_handler handler);
WLCLIENT_API_EXPORT void wlclient_set_mouse_move_handler(wlclient_window* window, wlclient_mouse_move_handler handler);

//======================================================================================================================
// INTERNALS - these expose internal state that is needed for custom backend development.
//======================================================================================================================

struct wl_display;

WLCLIENT_API_EXPORT struct wl_display* _wlclient_get_wl_display(void);
WLCLIENT_API_EXPORT wlclient_global_state* _wlclient_get_wl_global_state(void);
WLCLIENT_API_EXPORT wlclient_window_data* _wlclient_get_wl_window_data(const wlclient_window* window);

WLCLIENT_API_EXPORT void _wlclient_set_backend_shutdown(void (*shutdown)(void));
WLCLIENT_API_EXPORT void _wlclient_set_backend_destroy_window(void (*destroy_window)(const wlclient_window* window));
WLCLIENT_API_EXPORT void _wlclient_set_backend_resize_framebuffer(void (*resize_fb)(const wlclient_window* window, u32 framebuffer_width, u32 framebuffer_height));
WLCLIENT_API_EXPORT void _wlclient_set_backend_scale_change(void (*scale_change)(const wlclient_window* window, f32 factor));
