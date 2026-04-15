#pragma once

#include "types.h"
#include "unity.h"

static const wlclient_global_state ZEROED_OUT_GSTATE = {
    .preffered_pixel_format = -1
};

i32 basic_wlclient_init(void);
i32 basic_wlclient_shutdown_with_no_init(void);
