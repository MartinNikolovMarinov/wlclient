#include "t-tests.h"

i32 run_all_tests(void) {
    UNITY_BEGIN();

    RUN_TEST(basic_wlclient_init);
    RUN_TEST(basic_wlclient_shutdown_with_no_init);

    return UNITY_END();
}

i32 main(void) {
    return run_all_tests();
}
