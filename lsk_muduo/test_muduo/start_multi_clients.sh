#!/bin/bash
# =============================================================================
# Multi-Client Manual Test Script (迭代二 Day 5-7)
# =============================================================================
# 功能：批量启动多个 MockAgvClient 进程，连接到 GatewayServer
# 用途：手动验证多客户端并发场景
# 
# 使用方法：
#   1. 先启动服务器：./gateway_main --port 8000
#   2. 再运行此脚本：./start_multi_clients.sh [num_clients] [server_addr]
#
# 参数：
#   num_clients  - 启动的客户端数量（默认10）
#   server_addr  - 服务器地址（默认127.0.0.1:8000）
# =============================================================================

# 默认参数
NUM_CLIENTS=${1:-10}
SERVER_ADDR=${2:-"127.0.0.1:8000"}

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo ""
echo "========================================"
echo "Starting Multiple AGV Clients"
echo "========================================"
echo "Number of Clients: ${NUM_CLIENTS}"
echo "Server Address:    ${SERVER_ADDR}"
echo "========================================"
echo ""

# 检查 agv_client_main 是否存在
if [ ! -f "./bin/agv_client_main" ]; then
    echo -e "${RED}Error: ./bin/agv_client_main not found!${NC}"
    echo "Please compile the project first: cd build && make"
    exit 1
fi

# 创建日志目录
LOG_DIR="./logs/multi_client_test_$(date +%Y%m%d_%H%M%S)"
mkdir -p "${LOG_DIR}"

echo "Logs will be saved to: ${LOG_DIR}"
echo ""

# 存储进程PID
PIDS=()

# 启动客户端
for i in $(seq 1 ${NUM_CLIENTS}); do
    AGV_ID=$(printf "AGV-%03d" $i)
    LOG_FILE="${LOG_DIR}/${AGV_ID}.log"
    
    echo -e "${GREEN}[${i}/${NUM_CLIENTS}]${NC} Starting ${AGV_ID}..."
    
    # 启动客户端（后台运行，输出重定向到日志文件）
    ./bin/agv_client_main --id "${AGV_ID}" --server "${SERVER_ADDR}" \
        > "${LOG_FILE}" 2>&1 &
    
    PIDS+=($!)
    
    # 为了避免连接风暴，每个客户端间隔50ms启动
    sleep 0.05
done

echo ""
echo "========================================"
echo -e "${GREEN}✓ All ${NUM_CLIENTS} clients started${NC}"
echo "========================================"
echo ""
echo "Process IDs: ${PIDS[@]}"
echo ""
echo "Commands:"
echo "  - View logs:       tail -f ${LOG_DIR}/AGV-001.log"
echo "  - Stop all:        kill ${PIDS[@]}"
echo "  - Stop all (force): pkill -f agv_client_main"
echo ""

# 定义清理函数
cleanup() {
    echo ''
    echo 'Stopping all clients...'
    for pid in "${PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    exit 0
}

# 设置陷阱，Ctrl+C 时杀死所有子进程
trap cleanup SIGINT SIGTERM

# 等待用户按 Ctrl+C 或所有进程结束
echo "Press Ctrl+C to stop all clients..."
wait

echo ""
echo "All clients stopped."
