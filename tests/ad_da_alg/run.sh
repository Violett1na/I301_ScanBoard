#!/bin/sh
# run.sh —— ad_da_alg 宿主单测构建与运行(规范 8-1: 宿主测试先行)
# 用法: 仓库根目录执行  sh tests/ad_da_alg/run.sh
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

gcc -std=c99 -Wall -Wextra -O2 \
    -I "$ROOT/I301_code/App" \
    -I "$ROOT/I301_code/Port" \
    -I "$ROOT/I301_code/Board" \
    "$ROOT/tests/ad_da_alg/test_ad_da_alg.c" \
    "$ROOT/I301_code/App/ad_da_alg.c" \
    -o "$ROOT/tests/ad_da_alg/test_ad_da_alg"

"$ROOT/tests/ad_da_alg/test_ad_da_alg"
