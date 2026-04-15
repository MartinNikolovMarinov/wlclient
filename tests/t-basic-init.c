#include "t-tests.h"

#include "types.h"
#include "wl-client.h"

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
    TEST_ASSERT_NOT_NULL(s->preffered_pixel_format == WL_SHM_FORMAT_ARGB8888);

    for (i32 i = 0; i < WLCLIENT_WINDOWS_COUNT; i++) {
        TEST_ASSERT_EQUAL_MEMORY(&s->windows[i], &ZEROED_OUT_GSTATE.windows[i], sizeof(s->windows[i]));
    }

    TEST_ASSERT_NOT_NULL(s->input_device_count == 0);
    for (i32 i = 0; i < WLCLIENT_MAX_INPUT_DEVICES; i++) {
        TEST_ASSERT_EQUAL_MEMORY(&s->input_device[i], &ZEROED_OUT_GSTATE.input_device[i], sizeof(s->windows[i]));
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
