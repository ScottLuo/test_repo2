#!/usr/bin/env bash
#
# run_tests.sh - host 端单元测试运行脚本
#
# 编译并运行 led_fsm / ap_logic 两个纯逻辑模块的单元测试，
# 并用 gcov 统计被测源文件的行覆盖率（目标 >= 70%）。
#
# 用法：bash tests/host/run_tests.sh
# 依赖：host 端 gcc + gcov（无需 ESP 工具链）
#
set -euo pipefail

# 定位仓库根目录（脚本位于 tests/host/ 下）
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "${WORK_DIR}"' EXIT

CFLAGS="-Wall -Wextra -g --coverage -std=c99 -I ${REPO_ROOT}/main -I ${SCRIPT_DIR}"

echo "=================================================="
echo "  ESP8266 SoftAP LED Demo - Host Unit Tests"
echo "=================================================="

# 编译并运行 led_fsm 测试
echo
echo ">>> test_led_fsm"
gcc ${CFLAGS} "${REPO_ROOT}/main/led_fsm.c" "${SCRIPT_DIR}/test_led_fsm.c" \
    -o "${WORK_DIR}/test_led_fsm"
"${WORK_DIR}/test_led_fsm"

# 编译并运行 ap_logic 测试
echo
echo ">>> test_ap_logic"
gcc ${CFLAGS} "${REPO_ROOT}/main/ap_logic.c" "${SCRIPT_DIR}/test_ap_logic.c" \
    -o "${WORK_DIR}/test_ap_logic"
"${WORK_DIR}/test_ap_logic"

# 覆盖率统计（cd 到 gcda 所在目录，gcov 才能找到 profile）
echo
echo "=================================================="
echo "  Coverage"
echo "=================================================="
cd "${WORK_DIR}"
echo "--- led_fsm.c ---"
gcov "${WORK_DIR}/test_led_fsm-led_fsm.gcno" 2>/dev/null | grep -E "Lines executed" || true
echo "--- ap_logic.c ---"
gcov "${WORK_DIR}/test_ap_logic-ap_logic.gcno" 2>/dev/null | grep -E "Lines executed" || true
