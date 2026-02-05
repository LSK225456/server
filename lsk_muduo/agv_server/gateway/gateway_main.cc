#include "GatewayServer.h"
#include "../../muduo/base/Logger.h"
#include "../../muduo/net/EventLoop.h"
#include "../../muduo/net/InetAddress.h"
#include <iostream>
#include <cstdlib>

/**
 * @brief GatewayServer 主程序入口（迭代一：Day 1-2）
 * 
 * @note 功能验证：
 *       - 启动单 Reactor 模式的网关服务器
 *       - 监听指定端口（默认 9090）
 *       - 上行看门狗每 100ms 检查一次
 *       - 低电量自动触发充电指令
 * 
 * @note 使用方法：
 *       ./gateway_main [port]
 *       默认端口 9090
 */
int main(int argc, char* argv[]) {
    // 解析命令行参数
    uint16_t port = 9090;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::atoi(argv[1]));
    }
    
    // 打印启动信息
    std::cout << "========================================" << std::endl;
    std::cout << " AGV Gateway Server (Iteration 1)" << std::endl;
    std::cout << " Listening on port: " << port << std::endl;
    std::cout << " Watchdog: 100ms interval, 1s timeout" << std::endl;
    std::cout << " Low Battery Threshold: 20%" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 创建事件循环
    EventLoop loop;
    InetAddress listen_addr(port);
    
    // 创建并启动 GatewayServer
    agv::gateway::GatewayServer server(&loop, listen_addr, "AGV-Gateway");
    server.start();
    
    LOG_INFO << "GatewayServer running on port " << port;
    
    // 进入事件循环（阻塞，直到 loop.quit() 被调用）
    loop.loop();
    
    return 0;
}
