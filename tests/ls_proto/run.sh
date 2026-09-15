#!/bin/sh
# run.sh —— ls_proto 协议层宿主单测构建与运行(规范 8-1: 宿主测试先行)
# 用法: 仓库根目录执行  sh tests/ls_proto/run.sh
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

GCC="${GCC:-gcc}"

"$GCC" -std=c99 -Wall -Wextra -O2 \
    -I "$ROOT/I301_code/Proto/Inc" \
    -I "$ROOT/I301_code/Proto/Src" \
    "$ROOT/tests/ls_proto/test_ls_proto.c" \
    "$ROOT/I301_code/Proto/Src/ls_proto.c" \
    "$ROOT/I301_code/Proto/Src/ls_proto_trans.c" \
    "$ROOT/I301_code/Proto/Src/ls_proto_receive.c" \
    -o "$ROOT/tests/ls_proto/test_ls_proto"

"$ROOT/tests/ls_proto/test_ls_proto"