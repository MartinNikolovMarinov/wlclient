#include "t-tests.h"

i32 run_all_tests(void) {
    UNITY_BEGIN();

    basic_tests();

    return UNITY_END();
}

i32 main(void) {
    return run_all_tests();
}
