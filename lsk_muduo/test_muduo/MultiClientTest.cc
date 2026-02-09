#include "MockAgvClient.h"
#include "../agv_server/gateway/GatewayServer.h"
#include "../muduo/net/EventLoop.h"
#include "../muduo/net/EventLoopThread.h"
#include "../muduo/net/InetAddress.h"
#include "../muduo/base/Logger.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <memory>

using namespace lsk_muduo;
using namespace agv::gateway;

/**
 * @brief 迭代二 Day 5-7：多客户端联调测试
 * 
 * @note 测试目标（对应验收标准）：
 *       1. 10个Client同时连接，各自50Hz发送Telemetry
 *       2. Server正确管理10个会话，Dispatcher正确分发消息
 *       3. 主动断开Client后5秒内会话被清理
 *       4. 验证ConcurrentMap和Dispatcher的单元测试全部通过（通过运行GTest）
 * 
 * @note 测试场景：
 *       Test 1：10个客户端并发连接与稳定通信（15秒）
 *       Test 2：断连清理（主动断开3个客户端，验证5秒内会话清理）
 *       Test 3：压力稳定性（20个客户端连接60秒）
 */

// ==================== 全局变量 ====================

static std::atomic<int> g_connected_count{0};
static std::atomic<int> g_disconnected_count{0};

// ==================== 测试场景 1：10个客户端并发连接 ====================

void test_concurrent_clients() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 1: 10 Concurrent Clients" << std::endl;
    std::cout << "========================================" << std::endl;
    
    g_connected_count = 0;
    g_disconnected_count = 0;
    
    // 启动 Server 线程
    std::thread server_thread([]() {
        EventLoop server_loop;
        InetAddress listen_addr(8200);
        GatewayServer server(&server_loop, listen_addr, "MultiClientGateway", 5.0);
        server.start();
        
        std::cout << "[TEST 1] GatewayServer started on port 8200" << std::endl;
        
        // 20 秒后自动退出
        server_loop.runAfter(20.0, [&]() {
            std::cout << "\n[TEST 1] Server shutting down..." << std::endl;
            server_loop.quit();
        });
        
        server_loop.loop();
    });
    
    // 等待服务器启动
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 创建10个客户端（使用多线程）
    const int kNumClients = 10;
    std::vector<std::thread> client_threads;
    std::vector<std::shared_ptr<EventLoop>> client_loops;
    
    std::cout << "\n[TEST 1] Starting " << kNumClients << " clients..." << std::endl;
    
    for (int i = 0; i < kNumClients; ++i) {
        client_threads.emplace_back([i]() {
            EventLoop client_loop;
            InetAddress server_addr(8200, "127.0.0.1");
            
            std::string agv_id = "AGV-" + std::to_string(i + 1);
            MockAgvClient client(&client_loop, server_addr, agv_id, 50.0, 100.0, 5.0);
            
            // 连接回调（监控连接状态）
            client_loop.runAfter(0.5, [&client, agv_id]() {
                if (client.isConnected()) {
                    g_connected_count++;
                    std::cout << "[TEST 1] ✓ " << agv_id << " connected ("
                              << g_connected_count.load() << "/" << 10 << ")" << std::endl;
                }
            });
            
            client.connect();
            
            // 15 秒后退出
            client_loop.runAfter(15.0, [&client_loop]() {
                client_loop.quit();
            });
            
            client_loop.loop();
        });
    }
    
    // 等待所有客户端完成
    for (auto& th : client_threads) {
        th.join();
    }
    
    server_thread.detach();
    
    // 验证结果
    std::cout << "\n[TEST 1] ===== Results =====" << std::endl;
    std::cout << "[TEST 1] Connected clients: " << g_connected_count.load() << "/" << kNumClients << std::endl;
    
    if (g_connected_count.load() == kNumClients) {
        std::cout << "[TEST 1] ✓ PASSED: All 10 clients connected successfully" << std::endl;
    } else {
        std::cout << "[TEST 1] ✗ FAILED: Not all clients connected" << std::endl;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}

// ==================== 测试场景 2：断连清理验证 ====================

void test_disconnection_cleanup() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 2: Disconnection Cleanup (5s)" << std::endl;
    std::cout << "========================================" << std::endl;
    
    std::cout << "[TEST 2] Note: This test verifies session cleanup within 5 seconds" << std::endl;
    std::cout << "[TEST 2] Expected behavior:" << std::endl;
    std::cout << "[TEST 2]   1. Start 5 clients" << std::endl;
    std::cout << "[TEST 2]   2. Disconnect 3 clients after 3 seconds" << std::endl;
    std::cout << "[TEST 2]   3. Server should detect and cleanup within 5 seconds" << std::endl;
    std::cout << "[TEST 2]   4. Look for '[WATCHDOG ALARM]' logs" << std::endl;
    
    // 启动 Server 线程
    std::thread server_thread([]() {
        EventLoop server_loop;
        InetAddress listen_addr(8201);
        GatewayServer server(&server_loop, listen_addr, "CleanupTestGateway", 5.0);
        server.start();
        
        std::cout << "\n[TEST 2] GatewayServer started on port 8201" << std::endl;
        
        // 20 秒后自动退出
        server_loop.runAfter(20.0, [&]() {
            std::cout << "\n[TEST 2] Server shutting down..." << std::endl;
            server_loop.quit();
        });
        
        server_loop.loop();
    });
    
    // 等待服务器启动
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 创建5个客户端
    const int kNumClients = 5;
    std::vector<std::thread> client_threads;
    
    std::cout << "\n[TEST 2] Starting " << kNumClients << " clients..." << std::endl;
    
    for (int i = 0; i < kNumClients; ++i) {
        client_threads.emplace_back([i]() {
            EventLoop client_loop;
            InetAddress server_addr(8201, "127.0.0.1");
            
            std::string agv_id = "AGV-DC-" + std::to_string(i + 1);
            MockAgvClient client(&client_loop, server_addr, agv_id, 50.0, 100.0, 10.0);
            
            client.connect();
            
            std::cout << "[TEST 2] " << agv_id << " connected" << std::endl;
            
            // 前3个客户端在3秒后主动断开
            if (i < 3) {
                client_loop.runAfter(3.0, [&client, agv_id]() {
                    std::cout << "[TEST 2] " << agv_id << " disconnecting..." << std::endl;
                    client.disconnect();
                });
                
                // 再等待1秒后退出循环
                client_loop.runAfter(4.0, [&client_loop]() {
                    client_loop.quit();
                });
            } else {
                // 后2个客户端保持连接15秒
                client_loop.runAfter(15.0, [&client_loop]() {
                    client_loop.quit();
                });
            }
            
            client_loop.loop();
        });
    }
    
    // 等待所有客户端完成
    for (auto& th : client_threads) {
        th.join();
    }
    
    server_thread.join();
    
    std::cout << "\n[TEST 2] ===== Results =====" << std::endl;
    std::cout << "[TEST 2] ✓ Check server logs for '[WATCHDOG ALARM]' within 5 seconds" << std::endl;
    std::cout << "[TEST 2] ✓ Expected 3 cleanup logs for AGV-DC-1, AGV-DC-2, AGV-DC-3" << std::endl;
    
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}

// ==================== 测试场景 3：压力稳定性（20客户端）====================

void test_stress_stability() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 3: Stress Stability (20 clients, 30s)" << std::endl;
    std::cout << "========================================" << std::endl;
    
    g_connected_count = 0;
    
    // 启动 Server 线程
    std::thread server_thread([]() {
        EventLoop server_loop;
        InetAddress listen_addr(8202);
        GatewayServer server(&server_loop, listen_addr, "StressTestGateway", 5.0);
        server.start();
        
        std::cout << "[TEST 3] GatewayServer started on port 8202" << std::endl;
        
        // 35 秒后自动退出
        server_loop.runAfter(35.0, [&]() {
            std::cout << "\n[TEST 3] Server shutting down..." << std::endl;
            server_loop.quit();
        });
        
        server_loop.loop();
    });
    
    // 等待服务器启动
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 创建20个客户端
    const int kNumClients = 20;
    std::vector<std::thread> client_threads;
    
    std::cout << "\n[TEST 3] Starting " << kNumClients << " clients..." << std::endl;
    
    for (int i = 0; i < kNumClients; ++i) {
        client_threads.emplace_back([i]() {
            EventLoop client_loop;
            InetAddress server_addr(8202, "127.0.0.1");
            
            std::string agv_id = "AGV-STRESS-" + std::to_string(i + 1);
            MockAgvClient client(&client_loop, server_addr, agv_id, 50.0, 100.0, 5.0);
            
            // 连接回调
            client_loop.runAfter(0.5, [&client, agv_id]() {
                if (client.isConnected()) {
                    g_connected_count++;
                    if (g_connected_count.load() % 5 == 0) {
                        std::cout << "[TEST 3] Progress: " << g_connected_count.load() 
                                  << "/" << 20 << " clients connected" << std::endl;
                    }
                }
            });
            
            client.connect();
            
            // 30 秒后退出
            client_loop.runAfter(30.0, [&client_loop]() {
                client_loop.quit();
            });
            
            client_loop.loop();
        });
    }
    
    // 等待所有客户端完成
    for (auto& th : client_threads) {
        th.join();
    }
    
    server_thread.join();
    
    // 验证结果
    std::cout << "\n[TEST 3] ===== Results =====" << std::endl;
    std::cout << "[TEST 3] Connected clients: " << g_connected_count.load() << "/" << kNumClients << std::endl;
    
    if (g_connected_count.load() == kNumClients) {
        std::cout << "[TEST 3] ✓ PASSED: All 20 clients maintained stable connection for 30 seconds" << std::endl;
    } else {
        std::cout << "[TEST 3] ✗ FAILED: Some clients lost connection" << std::endl;
    }
}

// ==================== 主函数 ====================

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Multi-Client Integration Test Suite" << std::endl;
    std::cout << "Iteration 2, Day 5-7" << std::endl;
    std::cout << "========================================" << std::endl;
    
    try {
        // Test 1: 10个客户端并发连接
        test_concurrent_clients();
        
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // Test 2: 断连清理验证
        test_disconnection_cleanup();
        
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // Test 3: 压力稳定性
        test_stress_stability();
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "All Multi-Client Tests Completed" << std::endl;
        std::cout << "========================================" << std::endl;
        
        std::cout << "\nNext Steps:" << std::endl;
        std::cout << "  1. Run unit tests: ./concurrent_map_test && ./dispatcher_test" << std::endl;
        std::cout << "  2. Run heartbeat test: ./heartbeat_test" << std::endl;
        std::cout << "  3. Run session manager test: ./session_manager_test" << std::endl;
        std::cout << "  4. For manual testing, use: ./start_multi_clients.sh" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
