#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>

#include <unistd.h>

#include "debug.h"
#include "types.h"
#include "wl-client.h"
#include "wl-egl.h"

i32 main(void) {
    wlclient_error_code result_code;

    wlclient_log_set_level(WLCLIENT_LOG_LEVEL_TRACE);
    // wlclient_log_set_level(WLCLIENT_LOG_LEVEL_DEBUG);
    // wlclient_log_set_level(WLCLIENT_LOG_LEVEL_FATAL);
    result_code = wlclient_init();
    if (result_code != WLCLIENT_OK) {
        return (i32) result_code;
    }

    wlclient_window window;
    result_code = wlclient_create_window(200, 300, "Testing", &window);
    if (result_code != WLCLIENT_OK) {
        return (i32) result_code;
    }

    wlclient_egl_add_config_attr(EGL_SURFACE_TYPE, EGL_WINDOW_BIT);
    wlclient_egl_add_config_attr(EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT);
    wlclient_egl_add_config_attr(EGL_RED_SIZE, 8);
    wlclient_egl_add_config_attr(EGL_GREEN_SIZE, 8);
    wlclient_egl_add_config_attr(EGL_BLUE_SIZE, 8);
    wlclient_egl_add_config_attr(EGL_ALPHA_SIZE, 8);

    // EGL_OPENGL_ES_API
    result_code = wlclient_egl_init(EGL_OPENGL_API);
    if (result_code != WLCLIENT_OK) {
        return (i32) result_code;
    }

    wlclient_egl_add_context_attr(EGL_CONTEXT_MAJOR_VERSION, 4);
    wlclient_egl_add_context_attr(EGL_CONTEXT_MINOR_VERSION, 5);
    wlclient_egl_add_context_attr(EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR);

    result_code = wlclient_egl_config_window(&window);
    if (result_code != WLCLIENT_OK) {
        return (i32) result_code;
    }

    result_code = wlclient_egl_set_current_context(&window);
    if (result_code != WLCLIENT_OK) {
        return (i32) result_code;
    }

    glViewport(0, 0, 200, 300);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    result_code = wlclient_egl_swap_buffers(&window);
    if (result_code != WLCLIENT_OK) {
        return (i32) result_code;
    }

    sleep(1);

    wlclient_shutdown();
    return 0;
}
