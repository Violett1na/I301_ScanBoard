#!/bin/sh
# run.sh —— 强制套超时判定宿主单测(规范 8-1/8-2: uint32 回绕必测)
# 用法: 仓库根目录执行  sh tests/ls_force_wd/run.sh
# 本机 PATH 内无 gcc 时:
#   GCC=/c/Qt/Tools/mingw1310_64/bin/gcc.exe sh tests/ls_force_wd/run.sh
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

GCC="${GCC:-gcc}"

"$GCC" -std=c99 -Wall -Wextra -O2 \
    -I "$ROOT/I301_code/App" \
    -I "$ROOT/I301_code/Proto/Inc" \
    "$ROOT/tests/ls_force_wd/test_ls_force_wd.c" \
    -o "$ROOT/tests/ls_force_wd/test_ls_force_wd"

"$ROOT/tests/ls_force_wd/test_ls_force_wd"