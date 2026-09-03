#!/bin/sh
# check_layer_boundary.sh —— 分层边界检查(2026-09-02 可移植分层架构)
# 判据: App/ Bsp/ Proto/ 三层不得引用厂商层(生成头/ST HAL/USB 库)。
#   通过 = 无输出(退出码 0); 有输出即违规(退出码 1)。
# 用法: 仓库根目录执行  sh tools/check_layer_boundary.sh
# 说明: EIDE include 路径为 target 级, 无法按目录隔离, 故以本脚本
#   作为评审门(设计规范 §6 执行手段)。Arch 级白名单特性
#   (CMSIS-core: DWT/__NOP 等)允许出现在 DRV/APP, 此处仅提示不报错。

set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CODE="$ROOT/I301_code"
st=0

if [ ! -d "$CODE/App" ]; then
    echo "错误: 未找到 $CODE, 请在仓库根目录执行" >&2
    exit 2
fi

echo "== 1) 厂商头 include =="
if grep -rnE '#include[[:space:]]*"(main\.h|stm32g4|adc\.h|dac\.h|tim\.h|gpio\.h|dma\.h|opamp\.h|usart\.h|usb_device\.h|usbd_|core_cm|cmsis_)' \
    "$CODE/App" "$CODE/Bsp" "$CODE/Proto"
then
    st=1
fi

echo "== 2) HAL/厂商符号 =="
if grep -rnE 'HAL_[A-Z]|__HAL_|GPIO_PIN_SET|GPIO_PIN_RESET|GPIO_MODE_|_GPIO_Port|_Pin\b|Error_Handler|FLASH_TYPE|NVIC_SystemReset|__NVIC_' \
    "$CODE/App" "$CODE/Bsp" "$CODE/Proto"
then
    st=1
fi

echo "== 2b) 生成代码初始化符号 =="
if grep -rnE '\bMX_[A-Z]' \
    "$CODE/App" "$CODE/Bsp" "$CODE/Proto"
then
    st=1
fi

echo "== 3) Arch 级(CMSIS-core)使用提示(白名单, 仅提示) =="
grep -rnE 'DWT->|CoreDebug|__NOP|__USAT|__WFI' \
    "$CODE/App" "$CODE/Bsp" "$CODE/Proto" || true

if [ "$st" -eq 0 ]; then
    echo "边界检查通过。"
else
    echo "边界检查未通过, 见上方条目。" >&2
fi
exit "$st"
