#pragma once

#include "core_init.h"

void rendererInit();
[[nodiscard]] bool renderFrame();
void rendererShutdown();

struct Color {
    u8 r;
    u8 g;
    u8 b;
    u8 a;
};

void debug_renderClearFrameBuffer(Color color);
void debug_renderFillRect(Color color, i32 x, i32 y, i32 width, i32 height);
