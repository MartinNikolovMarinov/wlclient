#pragma once

#include "API.h"
#include "types.h"

#include <EGL/egl.h>

WLCLIENT_API_EXPORT void wlclient_egl_add_config_attr(EGLint key, EGLint value);
WLCLIENT_API_EXPORT void wlclient_egl_add_context_attr(EGLint key, EGLint value);

WLCLIENT_API_EXPORT wlclient_error_code wlclient_egl_set_swap_interval(i32 interval);

WLCLIENT_API_EXPORT wlclient_error_code wlclient_egl_init(EGLenum api);
WLCLIENT_API_EXPORT wlclient_error_code wlclient_egl_config_window(wlclient_window* window);

WLCLIENT_API_EXPORT wlclient_error_code wlclient_egl_make_current_context(wlclient_window* window);
WLCLIENT_API_EXPORT wlclient_error_code wlclient_egl_swap_buffers(const wlclient_window* window);
