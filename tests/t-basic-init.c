#include "t-tests.h"

#include "types.h"
#include "wl-client.h"

#include "wayland-client-protocol.h"

void check_zeroed_out_gstate() {
    wlclient_global_state* s = _wlclient_get_wl_global_state();
    TEST_ASSERT_EQUAL_MEMORY(s, &ZEROED_OUT_GSTATE, sizeof(*s));
}

void assert_global_state_initialized() {
    TEST_ASSERT_NOT_NULL(_wlclient_get_wl_display());
    wlclient_global_state* s = _wlclient_get_wl_global_state();

    TEST_ASSERT_NOT_NULL(s->allocator.alloc);
    TEST_ASSERT_NOT_NULL(s->allocator.strdup);

    TEST_ASSERT_NOT_NULL(s->display);
    TEST_ASSERT_NOT_NULL(s->registry);
    TEST_ASSERT_NOT_NULL(s->xdgWmBase);
    TEST_ASSERT_NOT_NULL(s->compositor);
    TEST_ASSERT_NOT_NULL(s->subcompositor);
    TEST_ASSERT_NOT_NULL(s->shm);
    TEST_ASSERT_TRUE(s->preffered_pixel_format == WL_SHM_FORMAT_ARGB8888);

    // Make sure no windows are currently in use:
    for (i32 i = 0; i < WLCLIENT_WINDOWS_COUNT; i++) {
        TEST_ASSERT_EQUAL_MEMORY(&s->windows[i], &ZEROED_OUT_GSTATE.windows[i], sizeof(s->windows[i]));
    }

    // Make sure the right amount of input devices are in use:
    TEST_ASSERT_TRUE(s->input_devices_count > 0);
    for (i32 i = 0; i < s->input_devices_count; i++) {
        TEST_ASSERT_TRUE(s->input_devices[i].used);
        TEST_ASSERT_NOT_NULL(s->input_devices[i].seat);
        TEST_ASSERT_NOT_NULL(s->input_devices[i].seat_name);
        TEST_ASSERT_NOT_NULL(s->input_devices[i].pointer);
        TEST_ASSERT_NOT_NULL(s->input_devices[i].keyboard);
    }
    for (i32 i = s->input_devices_count; i < WLCLIENT_MAX_INPUT_DEVICES; i++) {
        TEST_ASSERT_EQUAL_MEMORY(&s->input_devices[i], &ZEROED_OUT_GSTATE.input_devices[i], sizeof(s->windows[i]));
    }

    TEST_ASSERT_NULL(s->backend_shutdown);
    TEST_ASSERT_NULL(s->backend_destroy_window);
    TEST_ASSERT_NULL(s->backend_resize_window);
}

i32 basic_wlclient_init(void) {
    wlclient_error_code result_code = wlclient_init(NULL);
    TEST_ASSERT_TRUE(result_code == WLCLIENT_ERROR_OK);
    assert_global_state_initialized();
    wlclient_shutdown();
    check_zeroed_out_gstate();
    return 0;
}

i32 basic_wlclient_shutdown_with_no_init(void) {
    wlclient_shutdown();
    wlclient_shutdown();
    wlclient_shutdown();
    check_zeroed_out_gstate();
    return 0;
}

i32 basic_tests(void) {
    UNITY_SET_FILE();
    RUN_TEST(basic_wlclient_init);
    RUN_TEST(basic_wlclient_shutdown_with_no_init);
    return 0;
}
