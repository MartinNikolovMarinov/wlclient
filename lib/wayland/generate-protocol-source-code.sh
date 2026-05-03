#!/bin/bash

set -euo pipefail

function wrap_file() {
    local file="$1"
    local tmp
    tmp=$(mktemp) || return 1

    {
        printf '#include "compiler.h"\n\n'
        printf 'PRAGMA_WARNING_PUSH\n'
        printf 'PRAGMA_WARNING_SUPPRESS_ALL\n\n'
        cat "$file"
        printf '\nPRAGMA_WARNING_POP\n'
    } > "$tmp" && mv "$tmp" "$file"
}

wayland-scanner client-header xdg-shell.xml xdg-shell-client-protocol.h
wayland-scanner private-code xdg-shell.xml xdg-shell-client-protocol.c
wrap_file xdg-shell-client-protocol.h
wrap_file xdg-shell-client-protocol.c

wayland-scanner client-header wayland.xml wayland-client-protocol.h
wayland-scanner private-code wayland.xml wayland-client-protocol.c
wrap_file wayland-client-protocol.h
wrap_file wayland-client-protocol.c

wayland-scanner client-header fractional-scale-v1.xml fractional-scale-v1-client-protocol.h
wayland-scanner private-code fractional-scale-v1.xml fractional-scale-v1-client-protocol.c
wrap_file fractional-scale-v1-client-protocol.h
wrap_file fractional-scale-v1-client-protocol.c
