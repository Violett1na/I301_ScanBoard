#!/bin/sh
# run.sh —— 强制套看门狗宿主单测(规范 8-1/8-2: uint32 回绕必测)
# 用法: 仓库根目录执行  sh tests/ls_force_wd/run.sh
# 本机 PATH 内无 gcc 时:
#   GCC=/c/Qt/Tools/mingw1310_64/bin/gcc.exe sh tests/ls_force_wd/run.sh
# 两个用例:
#   1) test_ls_force_wd   —— 纯判定函数(不链接固件源文件, 无平台依赖);
#   2) test_force_release —— 释放逻辑, 链接 App 层胶水 TU 并桩其外部依赖。
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

GCC="${GCC:-gcc}"

"$GCC" -std=c99 -Wall -Wextra -O2 \
    -I "$ROOT/I301_code/App" \
    -I "$ROOT/I301_code/Proto/Inc" \
    "$ROOT/tests/ls_force_wd/test_ls_force_wd.c" \
    -o "$ROOT/tests/ls_force_wd/test_ls_force_wd"

"$ROOT/tests/ls_force_wd/test_ls_force_wd"

# 释放逻辑用例: 链接 App 层胶水 TU, 外部依赖由 stubs_ls_app_deps.c 全量
# 补齐(单一 .c 的未定义符号须全部解决才能链接, 本工具链 --gc-sections
# 实测不裁剪未调用的全局函数, 故不做该尝试)。
"$GCC" -std=c99 -Wall -Wextra -O2 \
    -I "$ROOT/I301_code/App" \
    -I "$ROOT/I301_code/Bsp" \
    -I "$ROOT/I301_code/Port" \
    -I "$ROOT/I301_code/Proto/Inc" \
    -I "$ROOT/I301_code/Proto/Src" \
    "$ROOT/tests/ls_force_wd/test_force_release.c" \
    "$ROOT/tests/ls_force_wd/stubs_ls_app_deps.c" \
    "$ROOT/I301_code/App/ls_proto_device_app.c" \
    "$ROOT/I301_code/Proto/Src/ls_proto.c" \
    "$ROOT/I301_code/Proto/Src/ls_proto_receive.c" \
    "$ROOT/I301_code/Proto/Src/ls_proto_trans.c" \
    -o "$ROOT/tests/ls_force_wd/test_force_release"

"$ROOT/tests/ls_force_wd/test_force_release"
