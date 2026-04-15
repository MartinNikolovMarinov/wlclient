#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>

#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "debug.h"
#include "types.h"
#include "wl-client.h"

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
    result_code = wlclient_init();
    if (result_code != WLCLIENT_ERROR_OK) {
        printf("ERROR - %d\n", result_code);
        goto done;
    }

done:
    wlclient_shutdown();
    return (i32) result_code;
}
