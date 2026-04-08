#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>

#include <stdio.h>
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
    if (result_code != WLCLIENT_ERROR_OK) {
        printf("ERROR - %d\n", result_code);
        goto error;
    }

    wlclient_window window;
    result_code = wlclient_create_window(200, 300, "Testing", 50, &window);
    if (result_code != WLCLIENT_ERROR_OK) {
        printf("ERROR - %d\n", result_code);
        goto error;
    }

    wlclient_egl_add_config_attr(EGL_SURFACE_TYPE, EGL_WINDOW_BIT);
    wlclient_egl_add_config_attr(EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT);
    wlclient_egl_add_config_attr(EGL_RED_SIZE, 8);
    wlclient_egl_add_config_attr(EGL_GREEN_SIZE, 8);
    wlclient_egl_add_config_attr(EGL_BLUE_SIZE, 8);
    wlclient_egl_add_config_attr(EGL_ALPHA_SIZE, 8);

    // EGL_OPENGL_ES_API
    result_code = wlclient_egl_init(EGL_OPENGL_API);
    if (result_code != WLCLIENT_ERROR_OK) {
        printf("ERROR - %d\n", result_code);
        goto error;
    }

    wlclient_egl_add_context_attr(EGL_CONTEXT_MAJOR_VERSION, 4);
    wlclient_egl_add_context_attr(EGL_CONTEXT_MINOR_VERSION, 5);
    wlclient_egl_add_context_attr(EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR);

    result_code = wlclient_egl_config_window(&window);
    if (result_code != WLCLIENT_ERROR_OK) {
        printf("ERROR - %d\n", result_code);
        goto error;
    }

    result_code = wlclient_egl_make_current_context(&window);
    if (result_code != WLCLIENT_ERROR_OK) {
        printf("ERROR - %d\n", result_code);
        goto error;
    }

    // Configure vsync:
    result_code = wlclient_egl_set_swap_interval(1);
    if (result_code != WLCLIENT_ERROR_OK) {
        printf("ERROR - %d\n", result_code);
        goto error;
    }

    i32 framebuffer_width = 0;
    i32 framebuffer_height = 0;
    wlclient_get_framebuffer_size(&window, &framebuffer_width, &framebuffer_height);

    glViewport(0, 0, framebuffer_width, framebuffer_height);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    result_code = wlclient_egl_swap_buffers(&window);
    if (result_code != WLCLIENT_ERROR_OK) {
        printf("ERROR - %d\n", result_code);
        goto error;
    }

    sleep(1);

    wlclient_shutdown();
    return 0;

error:
    wlclient_shutdown();
    return (i32) result_code;
}
