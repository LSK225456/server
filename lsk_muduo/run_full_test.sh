#!/bin/bash
# AGV性能完整测试脚本

cd /home/ubuntu2004/server/lsk_muduo

echo "================================="
echo "AGV Gateway Server 完整性能测试"
echo "CPU: Intel Core 5 210H (8核)"
echo "================================="
echo ""

# 清理函数
cleanup() {
    echo "[清理] 停止所有进程..."
    pkill -9 -f gateway_main 2>/dev/null
    pkill -9 -f agv_client_main 2>/dev/null
    sleep 2
}

# 测试函数
run_test() {
    local test_name=$1
    local server_args=$2
    local client_args=$3
    local duration=$4
    
    echo ""
    echo "========================================="
    echo "测试: $test_name"
    echo "========================================="
    
    cleanup
    
    # 启动服务器
    echo "[1/3] 启动服务器: $server_args"
    ./bin/gateway_main $server_args > /tmp/test_server.log 2>&1 &
    local server_pid=$!
    sleep 3
    
    # 检查服务器是否成功启动
    if ! ps -p $server_pid > /dev/null; then
        echo "[✗] 服务器启动失败！"
        cat /tmp/test_server.log | tail -10
        return 1
    fi
    echo "[✓] 服务器启动成功 (PID: $server_pid)"
    
    # 启动客户端
    echo "[2/3] 启动客户端: $client_args (运行${duration}秒)"
    timeout $duration ./bin/agv_client_main $client_args 2>&1 | tee /tmp/test_client.log | grep -E "WARN|ERROR|Configuration" | head -20 &
   CLIENT_PID=$!
    
    # 等待测试完成
    sleep $duration
    
    # 检查结果
    echo "[3/3] 分析结果..."
    
    # 检查看门狗超时
    local watchdog_count=$(grep -c "WATCHDOG" /tmp/test_client.log)
    # 检查ERROR数量
    local error_count=$(grep -c "ERROR" /tmp/test_client.log)
    # 检查电量变化
    local battery_changes=$(grep -c "BATTERY" /tmp/test_client.log)
    
    echo "  - 看门狗超时次数: $watchdog_count"
    echo "  - ERROR数量: $error_count"
    echo "  - 电量变化次数: $battery_changes"
    
    if [ $watchdog_count -eq 0 ] && [ $error_count -le 5 ]; then
        echo "[✓] 测试通过 - 系统稳定"
        return 0
    else
        echo "[✗] 测试失败 - 检测到问题"
        return 1
    fi
}

# 开始测试
echo "开始系统化测试..."

# 测试1: 单Reactor + 10Hz (优化后默认配置)
run_test "单Reactor + 10Hz + 40%电量" \
         "" \
         "--id TEST1 --battery 40.0" \
         30

TEST1_RESULT=$?

# 测试2: 单Reactor + 50Hz (旧配置)
run_test "单Reactor + 50Hz + 40%电量" \
         "" \
         "--id TEST2 --battery 40.0 --freq 50.0" \
         30

TEST2_RESULT=$?

# 测试3: 多Reactor + 10Hz
run_test "多Reactor(4线程) + 10Hz + 40%电量" \
         "--threads 4" \
         "--id TEST3 --battery 40.0" \
         30

TEST3_RESULT=$?

# 测试4: 多Reactor + 50Hz
run_test "多Reactor(4线程) + 50Hz + 40%电量" \
         "--threads 4" \
         "--id TEST4 --battery 40.0 --freq 50.0" \
         30

TEST4_RESULT=$?

# 测试5: 多Reactor + 100Hz (极限压测)
run_test "多Reactor(4线程) + 100Hz + 40%电量" \
         "--threads 4" \
         "--id TEST5 --battery 40.0 --freq 100.0" \
         30

TEST5_RESULT=$?

# 测试6: 充电流程测试
run_test "单Reactor + 10Hz + 18%电量(触发充电)" \
         "" \
         "--id TEST6 --battery 18.0" \
         30

TEST6_RESULT=$?

# 清理
cleanup

# 输出总结
echo ""
echo "================================="
echo "测试结果总结"
echo "================================="
[ $TEST1_RESULT -eq 0 ] && echo "[✓] 测试1: 单Reactor + 10Hz (优化后)" || echo "[✗] 测试1: 单Reactor + 10Hz"
[ $TEST2_RESULT -eq 0 ] && echo "[✓] 测试2: 单Reactor + 50Hz (旧配置)" || echo "[✗] 测试2: 单Reactor + 50Hz"
[ $TEST3_RESULT -eq 0 ] && echo "[✓] 测试3: 多Reactor + 10Hz" || echo "[✗] 测试3: 多Reactor + 10Hz"
[ $TEST4_RESULT -eq 0 ] && echo "[✓] 测试4: 多Reactor + 50Hz" || echo "[✗] 测试4: 多Reactor + 50Hz"
[ $TEST5_RESULT -eq 0 ] && echo "[✓] 测试5: 多Reactor + 100Hz (极限)" || echo "[✗] 测试5: 多Reactor + 100Hz"
[ $TEST6_RESULT -eq 0 ] && echo "[✓] 测试6: 充电流程测试" || echo "[✗] 测试6: 充电流程测试"
echo "================================="

# 结论
if [ $TEST1_RESULT -eq 0 ]; then
    echo ""
    echo "🎉 结论: 优化后的配置(10Hz + 0.2s心跳 + 8s超时)完全正常！"
    echo "   问题不在CPU性能，而在于之前的参数配置不合理。"
elif [ $TEST3_RESULT -eq 0 ]; then
    echo ""
    echo "⚠️  结论: 单Reactor有问题，但多Reactor正常。"
    echo "   建议使用多Reactor模式: ./bin/gateway_main --threads 4"
else
    echo ""
    echo "❌ 结论: 检测到代码问题，需要进一步调试。"
fi

echo ""
echo "详细日志: /tmp/test_server.log, /tmp/test_client.log"
