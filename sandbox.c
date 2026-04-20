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

void handle_close(wlclient_window* window) {
    printf("USER SPACE: Received close signal for window (id=%d)\n", window->id);
    g_running = 0;
}

void handle_size_change(wlclient_window* window, u32 width, u32 height) {
    printf("USER SPACE: Size change for window (id=%d) to width=%d and height=%d \n", window->id, width, height);
}

void handle_framebuffer_size_change(wlclient_window* window, u32 width, u32 height) {
    printf("USER SPACE: Framebuffer size change for window (id=%d) to width=%d and height=%d \n", window->id, width, height);
}

void handle_scale_factor_change(wlclient_window* window, f32 factor) {
    printf("USER SPACE: Scale factor change for window (id=%d) to factor=%f \n", window->id, (f64)factor);
}

void handle_mouse_move(struct wlclient_window* window, f64 x, f64 y) {
    printf("USER SPACE: Mouse move for window (id=%d) x=%f, y=%f \n", window->id, x, y);
}

i32 main(void) {
    signal(SIGINT, sigint_handler);

    wlclient_error_code result_code = 0;

    wlclient_log_set_level(WLCLIENT_LOG_LEVEL_DEBUG);
    result_code = wlclient_init(NULL);
    if (result_code != WLCLIENT_ERROR_OK) {
        printf("ERROR - %d\n", result_code);
        goto done;
    }

    // Create window
    wlclient_window window;
    {
        wlclient_window_decoration_config dcor_cfg = WLCLIENT_NO_DECORATION_CONFIG;
        dcor_cfg.edge_logical_thickness = 20;
        dcor_cfg.decor_logical_height = 50;
        dcor_cfg.decor_color = (wlclient_color) { .r = 0, .g = 255, .b = 255, .a = 255 };
        dcor_cfg.edge_color = (wlclient_color) { .r = 0, .g = 255, .b = 0, .a = 255 };
        result_code = wlclient_create_window(&window, 800, 600, "Example", &dcor_cfg);
        if (result_code != WLCLIENT_ERROR_OK) {
            printf("ERROR - %d\n", result_code);
            goto done;
        }
    }

    // Set window handlers
    {
        wlclient_set_close_handler(&window, handle_close);
        wlclient_set_size_change_handler(&window, handle_size_change);
        wlclient_set_framebuffer_change_handler(&window, handle_framebuffer_size_change);
        wlclient_set_scale_factor_change_handler(&window, handle_scale_factor_change);
        wlclient_set_mouse_move_handler(&window, handle_mouse_move);
    }

    // Configure EGL
    {
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
            goto done;
        }

        wlclient_egl_add_context_attr(EGL_CONTEXT_MAJOR_VERSION, 4);
        wlclient_egl_add_context_attr(EGL_CONTEXT_MINOR_VERSION, 5);
        wlclient_egl_add_context_attr(EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR);

        result_code = wlclient_egl_config_window(&window);
        if (result_code != WLCLIENT_ERROR_OK) {
            printf("ERROR - %d\n", result_code);
            goto done;
        }

        result_code = wlclient_egl_make_current_context(&window);
        if (result_code != WLCLIENT_ERROR_OK) {
            printf("ERROR - %d\n", result_code);
            goto done;
        }

        // Configure vsync:
        result_code = wlclient_egl_set_swap_interval(1);
        if (result_code != WLCLIENT_ERROR_OK) {
            printf("ERROR - %d\n", result_code);
            goto done;
        }
    }

    u32 fb_w, fb_h;
    wlclient_get_framebuffer(&window, &fb_w, &fb_h);

    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glViewport(0, 0, (i32)fb_w, (i32)fb_h);

    while (g_running) {
        result_code = wlclient_poll_events(100);
        if (result_code != WLCLIENT_ERROR_OK && result_code != WLCLIENT_ERROR_EVENT_POLL_TIMEOUT) {
            printf("POLLING FAILED ERROR - %d\n", result_code);
            goto done;
        }

        // wlclient_toggle_window_decor(&window);

        wlclient_get_framebuffer(&window, &fb_w, &fb_h);
        glViewport(0, 0, (i32)fb_w, (i32)fb_h);
        glClear(GL_COLOR_BUFFER_BIT);

        result_code = wlclient_egl_swap_buffers(&window);
        if (result_code != WLCLIENT_ERROR_OK) {
            printf("SWAP BUFFERS FAILED ERROR - %d\n", result_code);
            goto done;
        }

        // sleep(1);
    }

done:
    wlclient_shutdown();
    return (i32) result_code;
}
