#include "debug.h"
#include "types.h"
#include "wl-client.h"

i32 main(void) {
    wlclient_error_code result_code;

    wlclient_log_set_level(WLCLIENT_LOG_LEVEL_TRACE);
    // wlclient_log_set_level(WLCLIENT_LOG_LEVEL_FATAL);
    result_code = wlclient_init();
    if (result_code != WLCLIENT_OK) {
        return (i32) result_code;
    }

    wlclient_window window;
    result_code = wlclient_create_window(200, 300, "testing", &window);
    if (result_code != WLCLIENT_OK) {
        return (i32) result_code;
    }

    wlclient_destroy_window(&window);
    wlclient_shutdown();
    return 0;
}
