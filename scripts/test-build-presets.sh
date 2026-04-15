#!/bin/bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo_root/build"
test_executable="$build_dir/tests"

check_exit_code() {
    if [ $? -ne 0 ]; then
        echo "Build failed"
        exit 1
    fi
}

clean_build_dir() {
    rm -rf "$build_dir"
}

build_preset() {
    local preset="$1"

    echo "BUILD WITH PRESET = $preset"
    CFLAGS="-Werror" CXXFLAGS="-Werror" cmake -S "$repo_root" -B "$build_dir" --preset "$preset" -Werror
    check_exit_code
    cmake --build "$build_dir" --parallel
    check_exit_code
}

run_test_preset() {
    local preset="$1"

    clean_build_dir
    build_preset "$preset"

    if [ ! -x "$test_executable" ]; then
        echo "Test executable not found for preset = $preset"
        exit 1
    fi

    echo "RUN TESTS IN PRESET = $preset"
    "$test_executable"
}

get_configure_presets() {
    cmake --list-presets | sed -n 's/^[[:space:]]*"\([^"]*\)".*/\1/p'
}

run_tests_for_all_presets() {
    local preset

    while IFS= read -r preset; do
        run_test_preset "$preset"
    done < <(get_configure_presets)
}

run_tests_for_all_presets
clean_build_dir

echo
echo "SUCCESS!"
