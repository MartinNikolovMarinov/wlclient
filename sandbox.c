#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>

#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include "debug.h"
#include "types.h"
#include "wl-client.h"
#include "wl-egl.h"

static volatile sig_atomic_t g_running = 1;

static void sigint_handler(int sig) {
    (void)sig;
    g_running = 0;
}

void handle_close(void) {
    printf("Received close signal.\n");
    g_running = 0;
}

i32 main(void) {
    signal(SIGINT, sigint_handler);

    wlclient_error_code result_code;

    wlclient_log_set_level(WLCLIENT_LOG_LEVEL_DEBUG);
    result_code = wlclient_init();
    if (result_code != WLCLIENT_ERROR_OK) {
        printf("ERROR - %d\n", result_code);
        goto error;
    }

    wlclient_window window;
    wlclient_decoration_config decor_cfg = wlclient_no_decoration_config;
    decor_cfg.decor_height = 50;
    decor_cfg.edge_border_size = 10;
    result_code = wlclient_create_window(100, 200, "Testing", &decor_cfg, &window);
    if (result_code != WLCLIENT_ERROR_OK) {
        printf("ERROR - %d\n", result_code);
        goto error;
    }

    wlclient_set_close_handler(&window, handle_close);

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

    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);

    i32 fb_w = 0, fb_h = 0;
    wlclient_get_framebuffer_size(&window, &fb_w, &fb_h);
    glViewport(0, 0, fb_w, fb_h);
    // i32 frame_counter = 1;
    // wlclient_resize_window(&window, frame_counter, frame_counter*2);

    // TODO: Add a user-facing resize callback that fires from update_framebuffer_size so glViewport
    // can be set there instead of polling every frame.
    while (g_running) {
        result_code = wlclient_poll_events();
        if (result_code != WLCLIENT_ERROR_OK) {
            printf("ERROR - %d\n", result_code);
            goto error;
        }

        // frame_counter++;
        // wlclient_resize_window(&window, frame_counter, frame_counter*2);

        wlclient_get_framebuffer_size(&window, &fb_w, &fb_h);
        glViewport(0, 0, fb_w, fb_h);
        glClear(GL_COLOR_BUFFER_BIT);

        result_code = wlclient_egl_swap_buffers(&window);
        if (result_code != WLCLIENT_ERROR_OK) {
            printf("ERROR - %d\n", result_code);
            goto error;
        }

        // sleep(1);
    }

    wlclient_shutdown();
    return 0;

error:
    wlclient_shutdown();
    return (i32) result_code;
}
