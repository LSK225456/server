#!/bin/bash
# AGV项目清理脚本 - 杀掉所有残留进程并释放端口

echo "正在清理 AGV 项目残留进程..."

# 杀掉所有gateway_main进程
pkill -9 -f gateway_main
echo "✓ gateway_main 进程已清理"

# 杀掉所有agv_client_main进程
pkill -9 -f agv_client_main
echo "✓ agv_client_main 进程已清理"

# 等待端口释放
sleep 1

# 检查8000端口是否已释放
if lsof -i:8000 > /dev/null 2>&1; then
    echo "❌ 警告：8000端口仍被占用"
    lsof -i:8000
else
    echo "✓ 8000端口已释放"
fi

echo "清理完成！现在可以启动服务了"
