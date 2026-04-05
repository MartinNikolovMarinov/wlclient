#!/bin/bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo_root/build"

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

test_preset() {
    local preset="$1"

    clean_build_dir
    build_preset "$preset"

    if [ -f "$build_dir/CTestTestfile.cmake" ] || [ -d "$build_dir/Testing" ]; then
        pushd "$build_dir" >/dev/null
        echo "RUN TESTS IN PRESET = $preset"
        ctest --output-on-failure
        check_exit_code
        popd >/dev/null
    fi
}

run_tests_for_all_presets() {
    test_preset "debug"
    test_preset "release"
    test_preset "clang-debug"
    test_preset "clang-release"
    test_preset "gcc-debug"
    test_preset "gcc-release"
}

run_tests_for_all_presets
clean_build_dir

echo
echo "SUCCESS!"
