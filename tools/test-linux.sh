#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux -j4
ctest --test-dir build/linux --output-on-failure
QT_QPA_PLATFORM=offscreen build/linux/snip --smoke-test
