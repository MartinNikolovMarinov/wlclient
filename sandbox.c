#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>

#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include <xkbcommon/xkbcommon-keysyms.h>

#include "compiler.h"
#include "debug.h"
#include "types.h"
#include "wl-client.h"
#include "wl-egl.h"

static volatile sig_atomic_t g_running = 1;

static void sigint_handler(int sig) {
    (void)sig;
    g_running = 0;
}

PRAGMA_WARNING_PUSH
PRAGMA_WARNING_SUPPRESS_ALL

void handle_close(wlclient_window* window) {
    // printf("USER SPACE: Received close signal for window (id=%d)\n", window->id);
    g_running = 0;
}

void handle_size_change(wlclient_window* window, u32 width, u32 height) {
    // printf("USER SPACE: Size change for window (id=%d) to width=%d and height=%d \n", window->id, width, height);
}

void handle_framebuffer_size_change(wlclient_window* window, u32 width, u32 height) {
    // printf("USER SPACE: Framebuffer size change for window (id=%d) to width=%d and height=%d \n", window->id, width, height);
    glViewport(0, 0, (i32)width, (i32)height);
}

void handle_scale_factor_change(wlclient_window* window, f32 factor) {
    // printf("USER SPACE: Scale factor change for window (id=%d) to factor=%f \n", window->id, (f64)factor);
}

void handle_mouse_focus(struct wlclient_window* window, bool has_mouse_focus) {
    printf("USER SPACE: Mouse focus for window (id=%d) focused=%s\n", window->id, has_mouse_focus ? "true" : "false");
}

void handle_mouse_move(struct wlclient_window* window, f64 x, f64 y) {
    // printf("USER SPACE: Mouse move for window (id=%d) x=%f, y=%f \n", window->id, x, y);
}

void handle_mouse_press_handler(struct wlclient_window* window, u32 button, bool is_pressed, f64 x, f64 y) {
    // printf(
    //     "USER SPACE: Mouse press for window (id=%d) button=%d pressed=%s x=%f, y=%f\n",
    //     window->id, button, is_pressed ? "true" : "false", x, y
    // );
}

void handle_keyboard_focus(struct wlclient_window* window, bool has_keyboard_focus) {
    printf(
        "USER SPACE: Keyboard focus for window (id=%d) focused=%s\n",
        window->id, has_keyboard_focus ? "true" : "false"
    );
}

void handle_keyboard_key(struct wlclient_window* window, u32 keycode, u32 keysym, bool is_pressed, u32 modifiers) {
    printf(
        "USER SPACE: Keyboard key for window (id=%d) keycode=%u keysym=%u pressed=%s modifiers=%u\n",
        window->id, keycode, keysym, is_pressed ? "true" : "false", modifiers
    );

    if (keysym == XKB_KEY_Escape) {
        g_running = 0;
    }
}

void handle_keyboard_text(struct wlclient_window* window, const char* utf8, usize len) {
    printf("USER SPACE: Keyboard text for window (id=%d) len=%"PRIu64" utf8=\"%.*s\"\n", window->id, len, (i32)len, utf8);
}

void handle_keyboard_modifiers(struct wlclient_window* window, u32 modifiers) {
    printf(
        "USER SPACE: Keyboard modifiers for window (id=%d) flags=%u shift=%s ctrl=%s alt=%s super=%s caps=%s num=%s\n",
        window->id,
        modifiers,
        WLCLIENT_MOD_HAS(modifiers, WLCLIENT_MOD_SHIFT) ? "true" : "false",
        WLCLIENT_MOD_HAS(modifiers, WLCLIENT_MOD_CONTROL) ? "true" : "false",
        WLCLIENT_MOD_HAS(modifiers, WLCLIENT_MOD_ALT) ? "true" : "false",
        WLCLIENT_MOD_HAS(modifiers, WLCLIENT_MOD_SUPER) ? "true" : "false",
        WLCLIENT_MOD_HAS(modifiers, WLCLIENT_MOD_CAPS_LOCK) ? "true" : "false",
        WLCLIENT_MOD_HAS(modifiers, WLCLIENT_MOD_NUM_LOCK) ? "true" : "false"
    );
}

void handle_keyboard_repeat_info(struct wlclient_window* window, i32 rate, i32 delay) {
    printf(
        "USER SPACE: Keyboard repeat info for window (id=%d) rate=%d delay=%d\n",
        window->id, rate, delay
    );
}

PRAGMA_WARNING_POP

i32 main(void) {
    signal(SIGINT, sigint_handler);

    wlclient_error_code result_code = 0;

    wlclient_log_set_level(WLCLIENT_LOG_LEVEL_INFO);
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

        wlclient_set_mouse_focus_handler(&window, handle_mouse_focus);
        wlclient_set_mouse_move_handler(&window, handle_mouse_move);
        wlclient_set_mouse_press_handler(&window, handle_mouse_press_handler);

        wlclient_set_keyboard_focus_handler(&window, handle_keyboard_focus);
        wlclient_set_keyboard_key_handler(&window, handle_keyboard_key);
        wlclient_set_keyboard_text_handler(&window, handle_keyboard_text);
        wlclient_set_keyboard_modifiers_handler(&window, handle_keyboard_modifiers);
        wlclient_set_keyboard_repeat_info_handler(&window, handle_keyboard_repeat_info);
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
        // wlclient_toggle_window_decor(&window);

        glClear(GL_COLOR_BUFFER_BIT);

        result_code = wlclient_egl_swap_buffers(&window);
        if (result_code != WLCLIENT_ERROR_OK) {
            printf("SWAP BUFFERS FAILED ERROR - %d\n", result_code);
            goto done;
        }

        result_code = wlclient_poll_events(0);
        if (result_code != WLCLIENT_ERROR_OK && result_code != WLCLIENT_ERROR_EVENT_POLL_TIMEOUT) {
            printf("POLLING FAILED ERROR - %d\n", result_code);
            goto done;
        }

        // sleep(1);
    }

done:
    wlclient_shutdown();
    return (i32) result_code;
}
