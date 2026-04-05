#include "types.h"
#include "wl-client.h"

i32 main() {
    i32 ret = wlclient_create_window(100, 200, "test");
    return ret;
}
