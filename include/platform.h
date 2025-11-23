#pragma once

#include "core_init.h"

struct OpenWindowInfo {
    const char* name;
    i32 width, height;
    bool useSoftwareRendering;
};

void platformInit();
void platformShutdown();

void platformOpenOSWindow(const OpenWindowInfo& openInfo);
void platformPollEvents();

void platformCreateSoftRendCtx(struct SoftwareRenderingContext& out);
