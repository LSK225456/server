#!/bin/bash
# =============================================================================
# All Tests Runner (迭代二验收脚本)
# =============================================================================
# 功能：运行所有单元测试和集成测试，生成完整测试报告
# 对应验收标准：
#   1. ConcurrentMap单元测试通过
#   2. Dispatcher单元测试通过  
#   3. SessionManager单元测试通过
#   4. Heartbeat心跳测试通过
#   5. 多客户端联调测试通过
# =============================================================================

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 切换到 bin 目录
cd "$(dirname "$0")/../bin" || exit 1

echo ""
echo "========================================"
echo "  LSK_MUDUO Test Suite - Iteration 2"
echo "  Day 5-7: Multi-Client Integration"
echo "========================================"
echo ""

# 统计变量
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# 测试结果数组
declare -a TEST_RESULTS

# 运行单个测试的函数
run_test() {
    local test_name=$1
    local test_binary=$2
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    echo ""
    echo "----------------------------------------"
    echo -e "${BLUE}[${TOTAL_TESTS}] Running: ${test_name}${NC}"
    echo "----------------------------------------"
    
    if [ ! -f "${test_binary}" ]; then
        echo -e "${RED}✗ Test binary not found: ${test_binary}${NC}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        TEST_RESULTS+=("${RED}✗ ${test_name} - Binary not found${NC}")
        return 1
    fi
    
    # 运行测试，捕获退出码
    ./${test_binary}
    local exit_code=$?
    
    if [ ${exit_code} -eq 0 ]; then
        echo -e "${GREEN}✓ ${test_name} PASSED${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        TEST_RESULTS+=("${GREEN}✓ ${test_name}${NC}")
        return 0
    else
        echo -e "${RED}✗ ${test_name} FAILED (exit code: ${exit_code})${NC}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        TEST_RESULTS+=("${RED}✗ ${test_name}${NC}")
        return 1
    fi
}

# ==================== 第一阶段：单元测试 ====================

echo ""
echo "========================================" 
echo "Phase 1: Unit Tests"
echo "========================================"

# 1. ConcurrentMap 测试
run_test "ConcurrentMap Test" "concurrent_map_test"

# 2. ProtobufDispatcher 测试
run_test "Dispatcher Test" "dispatcher_test"

# 3. SessionManager 测试  
run_test "SessionManager Test" "session_manager_test"

# 4. Buffer 测试
run_test "Buffer Test" "buffer_test"

# 5. Codec 测试
run_test "Codec Test" "codec_test"

# ==================== 第二阶段：集成测试 ====================

echo ""
echo "========================================"
echo "Phase 2: Integration Tests"
echo "========================================"

# 6. Heartbeat 心跳测试（包含客户端/服务端看门狗）
run_test "Heartbeat Test" "heartbeat_test"

# 7. 多客户端联调测试（Day 5-7 核心验收）
run_test "Multi-Client Integration Test" "multi_client_test"

# ==================== 测试报告 ====================

echo ""
echo "========================================"
echo "  Test Results Summary"
echo "========================================"
echo ""

for result in "${TEST_RESULTS[@]}"; do
    echo -e "  ${result}"
done

echo ""
echo "----------------------------------------"
echo -e "Total Tests:   ${TOTAL_TESTS}"
echo -e "${GREEN}Passed Tests:  ${PASSED_TESTS}${NC}"
if [ ${FAILED_TESTS} -gt 0 ]; then
    echo -e "${RED}Failed Tests:  ${FAILED_TESTS}${NC}"
else
    echo -e "Failed Tests:  ${FAILED_TESTS}"
fi
echo "----------------------------------------"
echo ""

# ==================== 验收标准检查 ====================

echo "========================================"
echo "  Iteration 2 Acceptance Criteria"
echo "========================================"
echo ""

# 核心验收项检查
check_acceptance() {
    local test_name=$1
    local is_passed=$2
    
    if [ "${is_passed}" = "true" ]; then
        echo -e "${GREEN}✓${NC} ${test_name}"
    else
        echo -e "${RED}✗${NC} ${test_name}"
    fi
}

# 从测试结果中检查特定测试是否通过
is_test_passed() {
    local test_name=$1
    for result in "${TEST_RESULTS[@]}"; do
        if [[ "${result}" == *"${test_name}"* ]] && [[ "${result}" == *"✓"* ]]; then
            echo "true"
            return
        fi
    done
    echo "false"
}

check_acceptance "1. ConcurrentMap 单元测试通过" "$(is_test_passed 'ConcurrentMap')"
check_acceptance "2. Dispatcher 单元测试通过" "$(is_test_passed 'Dispatcher')"
check_acceptance "3. SessionManager 单元测试通过" "$(is_test_passed 'SessionManager')"
check_acceptance "4. Heartbeat 心跳超时测试通过" "$(is_test_passed 'Heartbeat')"
check_acceptance "5. 多客户端联调测试通过" "$(is_test_passed 'Multi-Client')"

echo ""

# ==================== 最终结论 ====================

if [ ${FAILED_TESTS} -eq 0 ]; then
    echo "========================================"
    echo -e "${GREEN}  ✓ ALL TESTS PASSED!${NC}"
    echo "========================================"
    echo ""
    echo "迭代二 Day 5-7 验收标准已达成："
    echo "  ✓ 10个Client同时连接，各自50Hz发送Telemetry"
    echo "  ✓ Server正确管理10个会话，Dispatcher正确分发消息"
    echo "  ✓ 主动断开Client后5秒内会话被清理"
    echo "  ✓ ConcurrentMap和Dispatcher的单元测试全部通过"
    echo ""
    exit 0
else
    echo "========================================"
    echo -e "${RED}  ✗ SOME TESTS FAILED${NC}"
    echo "========================================"
    echo ""
    echo "Please check the failed tests above and fix the issues."
    echo ""
    exit 1
fi
