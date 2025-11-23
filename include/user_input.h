#pragma once

#include "core_init.h"

using namespace coretypes;

enum struct KeyboardModifiers : u8 {
    MODNONE = 0,
    MODSHIFT = 1 << 0,
    MODCONTROL = 1 << 1,
    MODALT = 1 << 2,
    MODSUPER = 1 << 3,
};

inline constexpr KeyboardModifiers operator|(KeyboardModifiers lhs, KeyboardModifiers rhs) {
    return KeyboardModifiers(u8(lhs) | u8(rhs));
}

inline constexpr KeyboardModifiers operator&(KeyboardModifiers lhs, KeyboardModifiers rhs) {
    return KeyboardModifiers(u8(lhs) & u8(rhs));
}

inline constexpr KeyboardModifiers operator^(KeyboardModifiers lhs, KeyboardModifiers rhs) {
    return KeyboardModifiers(u8(lhs) ^ u8(rhs));
}

// Define compound assignment operators
inline constexpr KeyboardModifiers& operator|=(KeyboardModifiers& lhs, KeyboardModifiers rhs) {
    lhs = lhs | rhs;
    return lhs;
}

inline constexpr KeyboardModifiers& operator&=(KeyboardModifiers& lhs, KeyboardModifiers rhs) {
    lhs = lhs & rhs;
    return lhs;
}

inline constexpr KeyboardModifiers& operator^=(KeyboardModifiers& lhs, KeyboardModifiers rhs) {
    lhs = lhs ^ rhs;
    return lhs;
}

const char* keyModifiersToCstr(KeyboardModifiers m);

enum struct MouseButton : u8 {
    NONE,
    LEFT,
    MIDDLE,
    RIGHT,
};

const char* mouseButtonToCstr(MouseButton m);

enum struct MouseScrollDirection : u8 {
    NONE,
    UP,
    DOWN,
};

const char* mouseScrollDirection(MouseScrollDirection md);
