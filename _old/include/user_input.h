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

inline constexpr bool operator!(KeyboardModifiers m) { return ~u8(m); }

inline constexpr KeyboardModifiers operator|(KeyboardModifiers lhs, KeyboardModifiers rhs) {
    return KeyboardModifiers(u8(lhs) | u8(rhs));
}

inline constexpr KeyboardModifiers operator&(KeyboardModifiers lhs, KeyboardModifiers rhs) {
    return KeyboardModifiers(u8(lhs) & u8(rhs));
}

inline constexpr KeyboardModifiers operator^(KeyboardModifiers lhs, KeyboardModifiers rhs) {
    return KeyboardModifiers(u8(lhs) ^ u8(rhs));
}

inline constexpr bool operator==(KeyboardModifiers lhs, KeyboardModifiers rhs) {
    return u8(lhs) == u8(rhs);
}
inline constexpr bool operator!=(KeyboardModifiers lhs, KeyboardModifiers rhs) {
    return !(lhs == rhs);
}

inline constexpr bool operator==(KeyboardModifiers lhs, u8 rhs) {
    return u8(lhs) == rhs;
}
inline constexpr bool operator!=(KeyboardModifiers lhs, u8 rhs) {
    return !(lhs == rhs);
}
inline constexpr bool operator==(u8 lhs, KeyboardModifiers rhs) {
    return lhs == u8(rhs);
}
inline constexpr bool operator!=(u8 lhs, KeyboardModifiers rhs) {
    return !(lhs == rhs);
}

inline constexpr KeyboardModifiers operator~(KeyboardModifiers& lhs) {
    return KeyboardModifiers(~u8(lhs));
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
