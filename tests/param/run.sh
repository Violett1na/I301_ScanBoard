#!/bin/sh
# run.sh —— param 参数层宿主单测构建与运行(规范 8-1: 宿主测试先行)
# 用法: 仓库根目录执行  sh tests/param/run.sh
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

GCC="${GCC:-gcc}"

"$GCC" -std=c99 -Wall -Wextra -O2 \
    -I "$ROOT/I301_code/App" \
    -I "$ROOT/I301_code/Bsp" \
    -I "$ROOT/I301_code/Port" \
    "$ROOT/tests/param/test_param.c" \
    "$ROOT/I301_code/App/param.c" \
    -o "$ROOT/tests/param/test_param"

"$ROOT/tests/param/test_param"
