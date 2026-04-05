#include "debug.h"
#include "types.h"
#include "wl-client.h"

i32 main(void) {
    wlclient_log_set_level(WLCLIENT_LOG_LEVEL_FATAL);
    wlclient_error_code ret = wlclient_init();
    wlclient_shutdown();
    return (i32) ret;
}
