#pragma once

#include "core_init.h"
#include "user_input.h"

using WindowCloseCallback = void (*)();
using WindowResizeCallback = void (*)(i32 w, i32 h);
using WindowFocusCallback = void (*)(bool gain);

using KeyCallback = void (*)(bool isPress, u32 vkcode, u32 scancode, KeyboardModifiers mods);

using MouseClickCallback = void (*)(bool isPress, MouseButton button, i32 x, i32 y, KeyboardModifiers mods);
using MouseMoveCallback = void (*)(i32 x, i32 y);
using MouseScrollCallback = void (*)(MouseScrollDirection direction, i32 x, i32 y);
using MouseEnterOrLeaveCallback = void (*)(bool enter);

struct UserInputEvents {
    WindowCloseCallback windowCloseCallback = nullptr;
    WindowResizeCallback windowResizeCallback = nullptr;
    WindowFocusCallback windowFocusCallback = nullptr;

    KeyCallback keyCallback = nullptr;

    MouseClickCallback mouseClickCallback = nullptr;
    MouseMoveCallback mouseMoveCallback = nullptr;
    MouseScrollCallback mouseScrollCallback = nullptr;
    MouseEnterOrLeaveCallback mouseEnterOrLeaveCallback = nullptr;
};

struct OpenWindowInfo {
    const char* name;
    i32 width, height;
    bool useSoftwareRendering;
    UserInputEvents userInputEvents;
};

// TODO: Handle "High density surfaces (HiDPI)"

void platformInit();
void platformShutdown();

void platformOpenOSWindow(const OpenWindowInfo& openInfo);
void platformPollEvents();

void platformCreateSoftRendCtx(struct SoftwareRenderingContext& out);
