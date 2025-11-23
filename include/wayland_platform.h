#pragma once

#include "core_init.h"

enum struct PixelFormat {
    UNDEFINED,

    ARGB8888,

    SENTINEL
};

constexpr i32 getBytesPerPixel(PixelFormat pf) {
    switch (pf) {
        case PixelFormat::ARGB8888: return 4;

        case PixelFormat::UNDEFINED: [[fallthrough]];
        case PixelFormat::SENTINEL:  [[fallthrough]];
        default:
            Panic(false);
            return -1;
    }
}

struct FrameBuffer {
    i32 width;
    i32 height;
    PixelFormat pixelFormat;
    struct wl_buffer* wlBuffer;
    u8* data;
};

struct SoftwareRenderingContext {
    bool* bufferIsReady;
    struct wl_display* display;
    struct wl_surface* surface;
    core::ArrStatic<FrameBuffer, 3> frameBuffers;
};
