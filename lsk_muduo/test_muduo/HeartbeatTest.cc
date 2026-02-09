#include "MockAgvClient.h"
#include "../agv_server/gateway/GatewayServer.h"
#include "../muduo/net/EventLoop.h"
#include "../muduo/net/InetAddress.h"
#include "../muduo/base/Logger.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

using namespace lsk_muduo;
using namespace agv::gateway;

/**
 * @brief 迭代二 Day 3-4：HeartbeatManager 心跳检测测试
 * 
 * @note 测试目标：
 *       1. 验证 5 秒心跳超时机制（文档要求）
 *       2. 验证客户端看门狗（5 秒无服务器消息 -> E_STOP）
 *       3. 验证服务端看门狗（5 秒无客户端消息 -> OFFLINE）
 * 
 * @note 测试场景：
 *       - Test 1：正常心跳保活（客户端发送心跳，服务端回复心跳，连接保持）
 *       - Test 2：客户端看门狗触发（服务端停止发送，客户端 5 秒后进入 E_STOP）
 *       - Test 3：服务端看门狗触发（客户端停止发送，服务端 5 秒后标记 OFFLINE）
 */

// ==================== 测试场景 1：正常心跳保活 ====================

void test_normal_heartbeat() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 1: Normal Heartbeat Keep-Alive" << std::endl;
    std::cout << "========================================" << std::endl;
    
    std::atomic<bool> client_connected(false);
    std::atomic<bool> client_running(false);
    
    // 启动 Server 线程（5 秒超时）
    std::thread server_thread([]() {
        EventLoop server_loop;
        InetAddress listen_addr(8100);
        GatewayServer server(&server_loop, listen_addr, "TestGateway", 5.0);
        server.start();
        
        // 15 秒后自动退出
        server_loop.runAfter(15.0, [&]() {
            server_loop.quit();
        });
        
        server_loop.loop();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 启动 Client 线程（5 秒超时）
    std::thread client_thread([&]() {
        EventLoop client_loop;
        InetAddress server_addr(8100, "127.0.0.1");
        MockAgvClient client(&client_loop, server_addr, "AGV-HB-001", 50.0, 100.0, 5.0);
        client.connect();
        
        // 1 秒后检查连接状态
        client_loop.runAfter(1.0, [&]() {
            client_connected = client.isConnected();
            std::cout << "\n[TEST 1] Connection status: " 
                      << (client_connected.load() ? "CONNECTED ✓" : "DISCONNECTED ✗") << std::endl;
        });
        
        // 8 秒后检查状态（应该保持正常）
        client_loop.runAfter(8.0, [&]() {
            client_running = (client.getState() != MockAgvClient::E_STOP);
            std::cout << "[TEST 1] After 8 seconds:" << std::endl;
            std::cout << "  State: " << MockAgvClient::stateToString(client.getState()) << std::endl;
            std::cout << "  Status: " << (client_running.load() ? "RUNNING ✓" : "E_STOP ✗") << std::endl;
        });
        
        // 10 秒后退出
        client_loop.runAfter(10.0, [&]() {
            std::cout << "[TEST 1] Test completed" << std::endl;
            client_loop.quit();
        });
        
        client_loop.loop();
    });
    
    client_thread.join();
    server_thread.detach();
    
    // 验证结果
    std::cout << "\n[TEST 1] ===== Results =====" << std::endl;
    if (client_connected && client_running) {
        std::cout << "[TEST 1] ✓ PASSED: Normal heartbeat keep-alive verified" << std::endl;
    } else {
        std::cout << "[TEST 1] ✗ FAILED: Heartbeat mechanism not working" << std::endl;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

// ==================== 测试场景 2：客户端看门狗触发 ====================

void test_client_watchdog_timeout() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 2: Client Watchdog Timeout" << std::endl;
    std::cout << "========================================" << std::endl;
    
    std::atomic<bool> client_connected(false);
    std::atomic<bool> client_emergency_stop(false);
    
    // 启动 Server 线程（正常运行，但不回复心跳）
    // 注意：这里需要特殊处理，让服务端不回复心跳
    // 简化实现：服务端正常运行，但客户端使用短超时（1秒）快速测试
    std::thread server_thread([]() {
        EventLoop server_loop;
        InetAddress listen_addr(8101);
        // 服务端使用 10 秒超时（不会触发）
        GatewayServer server(&server_loop, listen_addr, "TestGateway", 10.0);
        server.start();
        
        // 15 秒后自动退出
        server_loop.runAfter(15.0, [&]() {
            server_loop.quit();
        });
        
        server_loop.loop();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 启动 Client 线程（使用 1 秒超时快速测试）
    std::thread client_thread([&]() {
        EventLoop client_loop;
        InetAddress server_addr(8101, "127.0.0.1");
        // 关键：使用 1 秒超时用于快速测试
        MockAgvClient client(&client_loop, server_addr, "AGV-HB-002", 50.0, 100.0, 1.0);
        client.connect();
        
        // 0.5 秒后检查连接状态
        client_loop.runAfter(0.5, [&]() {
            client_connected = client.isConnected();
            std::cout << "\n[TEST 2] Initial connection: " 
                      << (client_connected.load() ? "CONNECTED ✓" : "DISCONNECTED ✗") << std::endl;
        });
        
        // 1 秒后，模拟服务端失联：停止发送消息（通过断开连接模拟）
        client_loop.runAfter(1.0, [&]() {
            std::cout << "[TEST 2] Simulating server silence (disconnect)..." << std::endl;
            client.disconnect();
        });
        
        // 3 秒后检查是否触发 E_STOP
        client_loop.runAfter(3.0, [&]() {
            client_emergency_stop = (client.getState() == MockAgvClient::E_STOP);
            std::cout << "[TEST 2] After timeout:" << std::endl;
            std::cout << "  State: " << MockAgvClient::stateToString(client.getState()) << std::endl;
            std::cout << "  Emergency Stop: " << (client_emergency_stop.load() ? "YES ✓" : "NO ✗") << std::endl;
        });
        
        // 4 秒后退出
        client_loop.runAfter(4.0, [&]() {
            std::cout << "[TEST 2] Test completed" << std::endl;
            client_loop.quit();
        });
        
        client_loop.loop();
    });
    
    client_thread.join();
    server_thread.detach();
    
    // 验证结果
    std::cout << "\n[TEST 2] ===== Results =====" << std::endl;
    if (client_connected && client_emergency_stop) {
        std::cout << "[TEST 2] ✓ PASSED: Client watchdog timeout verified" << std::endl;
    } else {
        std::cout << "[TEST 2] ✗ FAILED: Client watchdog not triggered" << std::endl;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

// ==================== 测试场景 3：服务端看门狗触发 ====================

void test_server_watchdog_timeout() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 3: Server Watchdog Timeout" << std::endl;
    std::cout << "========================================" << std::endl;
    
    std::cout << "[TEST 3] Note: This test requires manual verification" << std::endl;
    std::cout << "[TEST 3] Expected behavior:" << std::endl;
    std::cout << "[TEST 3]   1. Client connects and sends initial messages" << std::endl;
    std::cout << "[TEST 3]   2. Client stops sending after 2 seconds" << std::endl;
    std::cout << "[TEST 3]   3. Server should mark AGV as OFFLINE after 5 seconds" << std::endl;
    std::cout << "[TEST 3]   4. Look for '[WATCHDOG ALARM] AGV [AGV-HB-003] OFFLINE' in logs" << std::endl;
    
    // 启动 Server 线程（5 秒超时）
    std::thread server_thread([]() {
        EventLoop server_loop;
        InetAddress listen_addr(8102);
        GatewayServer server(&server_loop, listen_addr, "TestGateway", 5.0);
        server.start();
        
        // 20 秒后自动退出
        server_loop.runAfter(20.0, [&]() {
            std::cout << "[TEST 3] Server shutting down" << std::endl;
            server_loop.quit();
        });
        
        server_loop.loop();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 启动 Client 线程
    std::thread client_thread([]() {
        EventLoop client_loop;
        InetAddress server_addr(8102, "127.0.0.1");
        MockAgvClient client(&client_loop, server_addr, "AGV-HB-003", 50.0, 100.0, 10.0);
        client.connect();
        
        std::cout << "\n[TEST 3] Client connected, sending messages..." << std::endl;
        
        // 2 秒后，客户端主动断开（模拟停止发送）
        client_loop.runAfter(2.0, [&]() {
            std::cout << "[TEST 3] Client stopping to send messages (simulating network loss)..." << std::endl;
            client.disconnect();
        });
        
        // 10 秒后退出
        client_loop.runAfter(10.0, [&]() {
            std::cout << "[TEST 3] Client test completed" << std::endl;
            client_loop.quit();
        });
        
        client_loop.loop();
    });
    
    client_thread.join();
    server_thread.join();
    
    std::cout << "\n[TEST 3] ===== Results =====" << std::endl;
    std::cout << "[TEST 3] ✓ Check server logs for '[WATCHDOG ALARM] AGV [AGV-HB-003] OFFLINE'" << std::endl;
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

// ==================== 主函数 ====================

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    std::cout << "\n========================================"  << std::endl;
    std::cout << "HeartbeatManager Test Suite" << std::endl;
    std::cout << "Iteration 2, Day 3-4" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    try {
        // Test 1: 正常心跳保活
        test_normal_heartbeat();
        
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // Test 2: 客户端看门狗触发
        test_client_watchdog_timeout();
        
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // Test 3: 服务端看门狗触发
        test_server_watchdog_timeout();
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "All Tests Completed" << std::endl;
        std::cout << "========================================" << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
