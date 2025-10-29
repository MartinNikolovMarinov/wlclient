#pragma once

#include "core_init.h"

struct OpenWindowInfo {
    const char* name;
    i32 width, height;
    bool initSoftwareRendering;
};

void platformInit();
void platformShutdown();

void platformOpenOSWindow(const OpenWindowInfo& openInfo);
void platformPollEvents();

struct SoftwareRenderingContext {
    bool* bufferIsReady;
    u8* memoryMappedArea;
    struct wl_display* display;
    struct wl_buffer* buffer;
    struct wl_surface* surface;
    i32 frameBufferWidth;
    i32 frameBufferHeight;
};

void platformCreateSoftRendCtx(SoftwareRenderingContext& out);
