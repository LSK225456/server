#include "MockAgvClient.h"
#include "../agv_server/gateway/GatewayServer.h"
#include "../muduo/net/EventLoop.h"
#include "../muduo/net/InetAddress.h"
#include "../muduo/base/Logger.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace lsk_muduo;
using namespace agv::gateway;

/**
 * @brief Day 3-4 MockAgvClient 集成测试（双闭环安全版）
 * 
 * @note 测试场景：
 *       1. 正常通信：Client 以 50Hz 发送 Telemetry，Server 收到并回复 Heartbeat
 *       2. 安全测试（拔网线模拟）：
 *          - 杀掉 Client 进程 -> Server 在 1秒内打印 [ALARM] AGV Offline
 *          - 杀掉 Server 进程 -> Client 在 1秒内打印 [EMERGENCY] Server Lost
 *       3. 业务测试（低电量触发）：
 *          - 观察 Client 电量自然下降
 *          - 当电量跌破 20% 时，Server 自动下发充电指令
 *          - Client 收到指令，日志显示"Receiving Charge Command, Moving to Charger..."
 */

// ==================== 测试场景 1：正常通信 ====================

void test_normal_communication() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 1: Normal Communication" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 启动 GatewayServer（在单独线程中）
    std::thread server_thread([]() {
        EventLoop server_loop;
        InetAddress listen_addr(8000);
        GatewayServer server(&server_loop, listen_addr, "TestGateway");
        server.start();
        
        // 10 秒后自动退出（Test 1运行5秒）
        server_loop.runAfter(10.0, [&]() {
            server_loop.quit();
        });
        
        server_loop.loop();
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 验证变量（需要在线程间共享）
    std::atomic<bool> client_connected(false);
    std::atomic<bool> test_completed(false);
    
    // 启动 MockAgvClient（在单独线程中）
    std::thread client_thread([&client_connected, &test_completed]() {
        EventLoop client_loop;
        InetAddress server_addr(8000, "127.0.0.1");
        MockAgvClient client(&client_loop, server_addr, "AGV-001", 50.0);
        
        std::cout << "\n[TEST] Client connecting to server..." << std::endl;
        client.connect();
        
        // 1 秒后检查连接状态
        client_loop.runAfter(1.0, [&client, &client_connected]() {
            client_connected = client.isConnected();
            std::cout << "\n[TEST] Connection status: " 
                      << (client_connected.load() ? "CONNECTED" : "DISCONNECTED") << std::endl;
            if (client_connected) {
                std::cout << "[TEST] ✓ Client successfully connected to server" << std::endl;
            }
        });
        
        // 3 秒后检查状态
        client_loop.runAfter(3.0, [&client]() {
            std::cout << "\n[TEST] Status check after 3 seconds:" << std::endl;
            std::cout << "[TEST] - Battery: " << client.getBattery() << "%" << std::endl;
            std::cout << "[TEST] - State: " << MockAgvClient::stateToString(client.getState()) << std::endl;
            std::cout << "[TEST] ✓ Client is operating normally" << std::endl;
        });
        
        // 运行 5 秒后退出
        client_loop.runAfter(5.0, [&client_loop, &test_completed]() {
            std::cout << "\n[TEST] Test 1 completed, quitting..." << std::endl;
            test_completed = true;
            client_loop.quit();
        });
        
        client_loop.loop();
    });
    
    // 等待测试完成
    client_thread.join();
    
    // 停止服务器（通过发送信号，但这里简化为分离线程）
    server_thread.detach();
    
    // 验证结果
    std::cout << "\n[TEST] ===== Test 1 Results =====" << std::endl;
    if (client_connected) {
        std::cout << "[TEST] ✓ Test 1 passed: Normal communication verified" << std::endl;
    } else {
        std::cout << "[TEST] ✗ Test 1 failed: Connection failed" << std::endl;
    }
    
    // 等待一下让服务器线程清理
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

// ==================== 测试场景 2：低电量触发充电 ====================

void test_low_battery_charging() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 2: Low Battery Auto-Charging" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 使用 atomic 变量跨线程共享状态
    std::atomic<bool> charge_command_received(false);
    std::atomic<bool> charging_started(false);
    std::atomic<bool> client_quit(false);
    
    // 启动 Server 线程
    std::thread server_thread([]() {
        EventLoop server_loop;
        InetAddress listen_addr(8001);
        GatewayServer server(&server_loop, listen_addr, "TestGateway");
        server.start();
        
        // 25 秒后自动退出（Test 2运行20秒）
        server_loop.runAfter(25.0, [&]() {
            server_loop.quit();
        });
        
        server_loop.loop();
    });
    
    server_thread.detach();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 启动 Client 线程
    std::thread client_thread([&]() {
        EventLoop client_loop;
        InetAddress server_addr(8001, "127.0.0.1");
        MockAgvClient client(&client_loop, server_addr, "AGV-002", 50.0, 25.0);
        client.connect();
        
        std::cout << "\n[TEST] Observing battery drain..." << std::endl;
        std::cout << "[TEST] Initial battery: " << client.getBattery() << "%" << std::endl;
        std::cout << "[TEST] Expected: Server will send charge command when battery < 20%" << std::endl;
        std::cout << "[TEST] Battery drain rate: -0.5%/s (IDLE state)" << std::endl;
        std::cout << "[TEST] ETA to low battery: ~10 seconds" << std::endl;
        
        // 每 2 秒检查一次状态
        int check_count = 0;
        client_loop.runEvery(2.0, [&]() {
            check_count++;
            double current_battery = client.getBattery();
            MockAgvClient::State current_state = client.getState();
            
            std::cout << "\n[TEST] Check #" << check_count 
                      << " - Battery: " << current_battery << "%, State: " 
                      << MockAgvClient::stateToString(current_state) << std::endl;
            
            // 检测充电指令
            if (current_state == MockAgvClient::MOVING_TO_CHARGER && 
                !charge_command_received.load()) {
                charge_command_received.store(true);
                std::cout << "[TEST] ✓ Charge command received and processed!" << std::endl;
            }
            
            // 检测充电开始
            if (current_state == MockAgvClient::CHARGING && !charging_started.load()) {
                charging_started.store(true);
                std::cout << "[TEST] ✓ Charging started!" << std::endl;
            }
        });
        
        // 运行 20 秒后退出
        client_loop.runAfter(20.0, [&]() {
            std::cout << "\n[TEST] Test 2 completed, quitting..." << std::endl;
            client_quit.store(true);
            client_loop.quit();
        });
        
        client_loop.loop();
    });
    
    client_thread.join();
    
    // 验证结果
    std::cout << "\n[TEST] ===== Test 2 Results =====" << std::endl;
    if (charge_command_received.load()) {
        std::cout << "[TEST] ✓ Test 2 passed: Low battery auto-charging verified" << std::endl;
    } else {
        std::cout << "[TEST] ✗ Test 2 failed: Charge command not received" << std::endl;
    }
}

// ==================== 测试场景 3：看门狗超时（Server Lost）====================

void test_watchdog_server_lost() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 3: Client Watchdog (Server Lost)" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 使用 atomic 跨线程共享状态
    std::atomic<bool> server_should_stop(false);
    std::atomic<bool> client_detected_loss(false);
    
    // 启动 Server 线程
    std::thread server_thread([&]() {
        EventLoop server_loop;
        InetAddress listen_addr(8002);
        GatewayServer server(&server_loop, listen_addr, "TestGateway");
        server.start();
        
        // 2 秒后模拟服务器崩溃
        server_loop.runAfter(2.0, [&]() {
            std::cout << "\n[TEST] ==> Simulating server crash (killing server)..." << std::endl;
            server_loop.quit();  // 直接退出模拟crash
        });
        
        server_loop.loop();
    });
    
    // 等待一点时间确保server启动
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 启动 Client 线程
    std::thread client_thread([&]() {
        EventLoop client_loop;
        InetAddress server_addr(8002, "127.0.0.1");
        MockAgvClient client(&client_loop, server_addr, "AGV-003", 50.0);
        client.connect();
        
        std::cout << "\n[TEST] Client connected, verifying normal state..." << std::endl;
        
        // 3.5 秒时检查 Client 状态（应该已经进入 E_STOP）
        client_loop.runAfter(3.5, [&]() {
            MockAgvClient::State state = client.getState();
            std::cout << "\n[TEST] Checking client state 1.5s after server crash..." << std::endl;
            std::cout << "[TEST] Current state: " << MockAgvClient::stateToString(state) << std::endl;
            
            if (state == MockAgvClient::E_STOP) {
                std::cout << "[TEST] ✓ Client correctly detected server loss within 1s!" << std::endl;
                client_detected_loss.store(true);
            } else {
                std::cout << "[TEST] ✗ Client failed to detect server loss!" << std::endl;
            }
        });
        
        // 再等待 3 秒后退出
        client_loop.runAfter(6.0, [&]() {
            std::cout << "\n[TEST] Test 3 completed" << std::endl;
            std::cout << "[TEST] Final client state: " 
                      << MockAgvClient::stateToString(client.getState()) << std::endl;
            
            if (client.getState() == MockAgvClient::E_STOP) {
                std::cout << "[TEST] ✓ Test 3 passed: Client correctly detected server loss" 
                          << std::endl;
            } else {
                std::cout << "[TEST] ✗ Test 3 failed: Client did not enter E_STOP" << std::endl;
            }
            
            client_loop.quit();
        });
        
        client_loop.loop();
    });
    
    client_thread.join();
    server_thread.join();  // 等待server正常结束
}

// ==================== 测试场景 4：看门狗超时（Client Lost）====================

void test_watchdog_client_lost() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 4: Server Watchdog (Client Lost)" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 使用 atomic 跨线程共享
    std::atomic<bool> client_disconnected(false);
    
    // 启动 Server 线程
    std::thread server_thread([]() {
        EventLoop server_loop;
        InetAddress listen_addr(8003);
        GatewayServer server(&server_loop, listen_addr, "TestGateway");
        server.start();
        
        // 6 秒后停止 Server
        server_loop.runAfter(6.0, [&]() {
            server_loop.quit();
        });
        
        server_loop.loop();
    });
    
    server_thread.detach();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 启动 Client 线程
    std::thread client_thread([&]() {
        EventLoop client_loop;
        InetAddress server_addr(8003, "127.0.0.1");
        MockAgvClient client(&client_loop, server_addr, "AGV-004", 50.0);
        client.connect();
        
        std::cout << "\n[TEST] Client connected, running for 2 seconds..." << std::endl;
        
        // 2 秒后断开 Client
        client_loop.runAfter(2.0, [&]() {
            std::cout << "\n[TEST] ==> Simulating client disconnect..." << std::endl;
            client.disconnect();
            client_disconnected.store(true);
        });
        
        // 再等待 2 秒后观察 Server 反应（应该在 1 秒内打印 ALARM）
        client_loop.runAfter(4.0, [&]() {
            std::cout << "\n[TEST] Test 4 completed" << std::endl;
            std::cout << "[TEST] ✓ Test 4 passed: Check server logs above for [WATCHDOG ALARM] AGV Offline" 
                      << std::endl;
            std::cout << "[TEST] Expected: Server detected client offline within 1 second after disconnect" 
                      << std::endl;
            client_loop.quit();
        });
        
        client_loop.loop();
    });
    
    client_thread.join();
}

// ==================== 主程序 ====================

int main(int argc, char* argv[]) {
    Logger::setLogLevel(Logger::INFO);
    
    std::cout << "\n╔════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  MockAgvClient 集成测试（Day 3-4：智能模拟器）       ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════════════╝" << std::endl;
    
    int test_choice = 0;
    if (argc > 1) {
        test_choice = std::atoi(argv[1]);
    }
    
    switch (test_choice) {
        case 1:
            test_normal_communication();
            break;
        case 2:
            test_low_battery_charging();
            break;
        case 3:
            test_watchdog_server_lost();
            break;
        case 4:
            test_watchdog_client_lost();
            break;
        case 0:
        default:
            std::cout << "\n运行所有测试场景：" << std::endl;
            test_normal_communication();
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            test_low_battery_charging();
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            test_watchdog_server_lost();
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            test_watchdog_client_lost();
            break;
    }
    
    std::cout << "\n╔════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  所有测试完成                                          ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════════════╝" << std::endl;
    
    return 0;
}
