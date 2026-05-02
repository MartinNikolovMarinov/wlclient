#include "t-tests.h"

#include "types.h"
#include "wl-client.h"

#include "wayland-client-protocol.h"

void check_zeroed_out_gstate() {
    wlclient_global_state* s = _wlclient_get_wl_global_state();
    TEST_ASSERT_EQUAL_MEMORY(s, &ZEROED_OUT_GSTATE, sizeof(*s));
}

void assert_window_state_initialized(
    const wlclient_window* window,
    u32 requested_content_width,
    u32 requested_content_height,
    const wlclient_window_decoration_config* decor_cfg
) {
    TEST_ASSERT_NOT_NULL(window);
    TEST_ASSERT_TRUE(window->id >= 0);
    TEST_ASSERT_TRUE(window->id < WLCLIENT_WINDOWS_COUNT);

    wlclient_global_state* s = _wlclient_get_wl_global_state();
    wlclient_window_data* wdata = &s->windows[window->id];

    TEST_ASSERT_TRUE(wdata->used);
    TEST_ASSERT_NOT_NULL(wdata->surface);
    TEST_ASSERT_NOT_NULL(wdata->xdg_surface);
    TEST_ASSERT_NOT_NULL(wdata->xdg_toplevel);

    // Make sure packet data was cleared after configure
    TEST_ASSERT_TRUE(wdata->toplevel_config_in_flight_packet.window_logical_width == 0);
    TEST_ASSERT_TRUE(wdata->toplevel_config_in_flight_packet.window_logical_height == 0);

    const u32 expected_edge  = (decor_cfg && decor_cfg->edge_logical_thickness > 0) ? decor_cfg->edge_logical_thickness : 0;
    const u32 expected_decor = (decor_cfg && decor_cfg->decor_logical_height > 0)  ? decor_cfg->decor_logical_height  : 0;

    const u32 requested_window_w = requested_content_width + (2 * expected_edge);
    const u32 requested_window_h = requested_content_height + (2 * expected_edge) + expected_decor;

    // Decoration config was applied verbatim.
    TEST_ASSERT_EQUAL_UINT32(expected_edge, wdata->edge_logical_thickness);
    TEST_ASSERT_EQUAL_UINT32(expected_decor, wdata->decor_logical_height);
    TEST_ASSERT_EQUAL(expected_decor == 0, wdata->csd_hidden);

    // Scale factor defaults to 1 per wl_surface spec — preferred_buffer_scale cannot
    // fire before the surface is mapped, and we do not map inside create_window.
    TEST_ASSERT_EQUAL_FLOAT(1.f, wdata->scale_factor);

    // The initial xdg_toplevel.configure is compositor-driven. Some compositors
    // keep the requested size by sending 0,0; others immediately force a
    // different logical size. Assert internal consistency instead of an exact
    // requested-size match.
    TEST_ASSERT_TRUE(wdata->window_logical_width > 0);
    TEST_ASSERT_TRUE(wdata->window_logical_height > 0);
    TEST_ASSERT_TRUE(wdata->window_logical_width >= requested_window_w);
    TEST_ASSERT_TRUE(wdata->window_logical_height >= requested_window_h);
    TEST_ASSERT_EQUAL_UINT32(
        wdata->window_logical_width - (2 * expected_edge),
        wdata->content_logical_width
    );
    TEST_ASSERT_EQUAL_UINT32(
        wdata->window_logical_height - ((2 * expected_edge) + expected_decor),
        wdata->content_logical_height
    );
    TEST_ASSERT_EQUAL_UINT32(wdata->content_logical_width, wdata->framebuffer_pixel_width);
    TEST_ASSERT_EQUAL_UINT32(wdata->content_logical_height, wdata->framebuffer_pixel_height);

    TEST_ASSERT_TRUE(wdata->content_logical_width  <= wdata->window_logical_width);
    TEST_ASSERT_TRUE(wdata->content_logical_height <= wdata->window_logical_height);

    TEST_ASSERT_NULL(wdata->close_handler);
    TEST_ASSERT_NULL(wdata->size_change_handler);
    TEST_ASSERT_NULL(wdata->framebuffer_change_handler);
    TEST_ASSERT_NULL(wdata->scale_factor_change_handler);
    TEST_ASSERT_NULL(wdata->suspended_handler);
    TEST_ASSERT_NULL(wdata->fullscreen_handler);
    TEST_ASSERT_NULL(wdata->mouse_focus_handler);
    TEST_ASSERT_NULL(wdata->mouse_move_handler);
    TEST_ASSERT_NULL(wdata->mouse_press_handler);
    TEST_ASSERT_NULL(wdata->keyboard_focus_handler);
    TEST_ASSERT_NULL(wdata->keyboard_key_handler);
    TEST_ASSERT_NULL(wdata->keyboard_text_handler);
    TEST_ASSERT_NULL(wdata->keyboard_modifiers_handler);
    TEST_ASSERT_NULL(wdata->keyboard_repeat_info_handler);
}

void assert_global_state_initialized() {
    TEST_ASSERT_NOT_NULL(_wlclient_get_wl_display());
    wlclient_global_state* s = _wlclient_get_wl_global_state();

    TEST_ASSERT_NOT_NULL(s->allocator.alloc);
    TEST_ASSERT_NOT_NULL(s->allocator.free);
    TEST_ASSERT_NOT_NULL(s->allocator.strdup);

    TEST_ASSERT_NOT_NULL(s->display);
    TEST_ASSERT_NOT_NULL(s->registry);
    TEST_ASSERT_NOT_NULL(s->xdgWmBase);
    TEST_ASSERT_NOT_NULL(s->compositor);
    TEST_ASSERT_NOT_NULL(s->subcompositor);
    TEST_ASSERT_NOT_NULL(s->shm);
    TEST_ASSERT_TRUE(s->preferred_pixel_format == WL_SHM_FORMAT_ARGB8888);

    // Make sure no windows are currently in use:
    for (i32 i = 0; i < WLCLIENT_WINDOWS_COUNT; i++) {
        TEST_ASSERT_EQUAL_MEMORY(&s->windows[i], &ZEROED_OUT_GSTATE.windows[i], sizeof(s->windows[i]));
    }

    // Make sure the that devices that are in use are configured:
    TEST_ASSERT_TRUE(s->input_devices_count >= 0);
    for (i32 i = 0; i < s->input_devices_count; i++) {
        TEST_ASSERT_TRUE(s->input_devices[i].used);
        TEST_ASSERT_NOT_NULL(s->input_devices[i].seat);
        TEST_ASSERT_NOT_NULL(s->input_devices[i].seat_name);
        TEST_ASSERT_TRUE(s->input_devices[i].seat_version > 0);
        TEST_ASSERT_NULL(s->input_devices[i].keyboard_target_surface);
        TEST_ASSERT_NULL(s->input_devices[i].xkb.context);
        TEST_ASSERT_NULL(s->input_devices[i].xkb.keymap);
        TEST_ASSERT_NULL(s->input_devices[i].xkb.state);
        TEST_ASSERT_NULL(s->input_devices[i].xkb.compose_table);
        TEST_ASSERT_NULL(s->input_devices[i].xkb.compose_state);
        // TEST_ASSERT_NOT_NULL(s->input_devices[i].pointer);
        // TEST_ASSERT_NOT_NULL(s->input_devices[i].keyboard);
    }
    for (i32 i = s->input_devices_count; i < WLCLIENT_MAX_INPUT_DEVICES; i++) {
        TEST_ASSERT_EQUAL_MEMORY(&s->input_devices[i], &ZEROED_OUT_GSTATE.input_devices[i], sizeof(s->input_devices[i]));
    }

    TEST_ASSERT_NULL(s->backend_shutdown);
    TEST_ASSERT_NULL(s->backend_destroy_window);
    TEST_ASSERT_NULL(s->backend_resize_framebuffer);
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

i32 basic_wlclient_create_window(void) {
    wlclient_error_code result_code = wlclient_init(NULL);
    TEST_ASSERT_TRUE(result_code == WLCLIENT_ERROR_OK);

    const u32 content_width  = 640;
    const u32 content_height = 480;
    const wlclient_window_decoration_config decor_cfg = {
        .decor_logical_height   = 30,
        .edge_logical_thickness = 2,
    };

    wlclient_window window = { .id = -1 };
    result_code = wlclient_create_window(
        &window,
        content_width, content_height,
        "create-window-test",
        &decor_cfg
    );
    TEST_ASSERT_TRUE(result_code == WLCLIENT_ERROR_OK);
    assert_window_state_initialized(&window, content_width, content_height, &decor_cfg);

    wlclient_shutdown();
    check_zeroed_out_gstate();
    return 0;
}

i32 basic_wlclient_create_window_no_decoration(void) {
    wlclient_error_code result_code = wlclient_init(NULL);
    TEST_ASSERT_TRUE(result_code == WLCLIENT_ERROR_OK);

    const u32 content_width  = 640;
    const u32 content_height = 480;

    wlclient_window window = { .id = -1 };
    result_code = wlclient_create_window(
        &window,
        content_width, content_height,
        "create-window-no-decor-test",
        &WLCLIENT_NO_DECORATION_CONFIG
    );
    TEST_ASSERT_TRUE(result_code == WLCLIENT_ERROR_OK);
    assert_window_state_initialized(&window, content_width, content_height, &WLCLIENT_NO_DECORATION_CONFIG);

    wlclient_shutdown();
    check_zeroed_out_gstate();
    return 0;
}

i32 basic_destroy_never_created_window(void) {
    wlclient_error_code result_code = wlclient_init(NULL);
    TEST_ASSERT_TRUE(result_code == WLCLIENT_ERROR_OK);

    // A window struct that was never passed through wlclient_create_window.
    // Contract: destroying it is a no-op that leaves global state untouched.
    wlclient_window window = { .id = -1 };
    wlclient_destroy_window(&window);

    TEST_ASSERT_EQUAL_INT32(-1, window.id);

    wlclient_global_state* s = _wlclient_get_wl_global_state();
    for (i32 i = 0; i < WLCLIENT_WINDOWS_COUNT; i++) {
        TEST_ASSERT_EQUAL_MEMORY(&s->windows[i], &ZEROED_OUT_GSTATE.windows[i], sizeof(s->windows[i]));
    }

    wlclient_shutdown();
    check_zeroed_out_gstate();
    return 0;
}

i32 basic_tests(void) {
    UNITY_SET_FILE();
    RUN_TEST(basic_wlclient_init);
    RUN_TEST(basic_wlclient_shutdown_with_no_init);
    RUN_TEST(basic_wlclient_create_window);
    RUN_TEST(basic_wlclient_create_window_no_decoration);
    RUN_TEST(basic_destroy_never_created_window);
    return 0;
}
