#pragma once

#include "types.h"
#include "unity.h"

/* Sets Unity.TestFile to the current translation unit so failure output
   reports the correct file. Call as the first line of every test function. */
#define UNITY_SET_FILE() (Unity.TestFile = __FILE__)

static const wlclient_global_state ZEROED_OUT_GSTATE = {
    .preferred_pixel_format = -1
};

i32 basic_tests(void);
