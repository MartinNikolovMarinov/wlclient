#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
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
    srand((u32) time(0));

    wlclient_error_code result_code = 0;

    wlclient_log_set_level(WLCLIENT_LOG_LEVEL_DEBUG);
    result_code = wlclient_init(NULL);
    if (result_code != WLCLIENT_ERROR_OK) {
        printf("ERROR - %d\n", result_code);
        goto done;
    }

    {
        wlclient_egl_add_config_attr(EGL_SURFACE_TYPE, EGL_WINDOW_BIT);
        wlclient_egl_add_config_attr(EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT);
        wlclient_egl_add_config_attr(EGL_RED_SIZE, 8);
        wlclient_egl_add_config_attr(EGL_GREEN_SIZE, 8);
        wlclient_egl_add_config_attr(EGL_BLUE_SIZE, 8);
        wlclient_egl_add_config_attr(EGL_ALPHA_SIZE, 8);

        wlclient_egl_add_context_attr(EGL_CONTEXT_MAJOR_VERSION, 4);
        wlclient_egl_add_context_attr(EGL_CONTEXT_MINOR_VERSION, 5);
        wlclient_egl_add_context_attr(EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR);

        // EGL_OPENGL_ES_API
        result_code = wlclient_egl_init(EGL_OPENGL_API);
        if (result_code != WLCLIENT_ERROR_OK) {
            printf("ERROR - %d\n", result_code);
            goto done;
        }
    }

    wlclient_window window;
    wclient_window_decoration_config dcor_cfg = WCLIENT_NO_DECORATION_CONFIG;
    dcor_cfg.edge_logical_thinkness = 5;
    dcor_cfg.decor_logical_height = 10;
    result_code = wlclient_create_window(&window, 200, 300, "Example", &dcor_cfg);
    if (result_code != WLCLIENT_ERROR_OK) {
        printf("ERROR - %d\n", result_code);
        goto done;
    }

done:
    wlclient_shutdown();
    return (i32) result_code;
}
