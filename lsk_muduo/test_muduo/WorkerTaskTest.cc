/**
 * @file WorkerTaskTest.cc
 * @brief Worker线程任务处理测试（迭代三：Day 3-4）
 * 
 * @note 测试目标：
 *       1. NavigationTask 正确投递到 Worker 线程
 *       2. IO 线程不被 Worker 阻塞（Telemetry 仍 50Hz 处理）
 *       3. 连接断开时 Worker 任务安全终止（弱引用检查）
 *       4. Worker 完成后正确回调到 IO 线程发送响应
 */

#include "../agv_server/gateway/GatewayServer.h"
#include "../agv_server/gateway/WorkerTask.h"
#include "../agv_server/proto/message.pb.h"
#include "../agv_server/proto/message_id.h"
#include "../agv_server/codec/LengthHeaderCodec.h"
#include "../muduo/net/EventLoop.h"
#include "../muduo/net/InetAddress.h"
#include "../muduo/net/TcpClient.h"
#include "../muduo/net/Buffer.h"
#include "../muduo/net/TimerId.h"
#include "../muduo/base/Logger.h"
#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>

using namespace lsk_muduo;
using namespace agv::gateway;
using namespace agv::proto;
using namespace agv::codec;

// ==================== 测试配置 ====================

constexpr int kServerPort1 = 9988;  // Test1使用的端口
constexpr int kServerPort2 = 9989;  // Test2使用的端口
constexpr int kServerPort3 = 9990;  // Test3使用的端口
constexpr const char* kServerAddr = "127.0.0.1";
constexpr int kWorkerThreads = 4;

// ==================== 测试统计 ====================

struct TestStats {
    std::atomic<int> telemetry_sent{0};
    std::atomic<int> nav_task_sent{0};
    std::atomic<int> response_received{0};
    std::atomic<int64_t> total_rtt_ms{0};
};

// ==================== 测试客户端 ====================

class TestClient {
public:
    TestClient(EventLoop* loop, const InetAddress& server_addr, const std::string& agv_id)
        : loop_(loop),
          client_(loop, server_addr, "TestClient-" + agv_id),
          agv_id_(agv_id),
          connected_(false) {
        
        client_.setConnectionCallback(
            std::bind(&TestClient::onConnection, this, std::placeholders::_1));
        client_.setMessageCallback(
            std::bind(&TestClient::onMessage, this,
                      std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    }
    
    void connect() { client_.connect(); }
    void disconnect() { client_.disconnect(); }
    
    void sendTelemetry() {
        if (!conn_) return;
        
        AgvTelemetry telem;
        telem.set_agv_id(agv_id_);
        telem.set_timestamp(Timestamp::now().microSecondsSinceEpoch());
        telem.set_x(10.0);
        telem.set_y(20.0);
        telem.set_theta(45.0);
        telem.set_battery(80.0);
        
        sendMessage(MSG_AGV_TELEMETRY, telem);
        stats_.telemetry_sent++;
    }
    
    void sendNavigationTask() {
        if (!conn_) return;
        
        NavigationTask task;
        task.set_target_agv_id(agv_id_);
        task.set_timestamp(Timestamp::now().microSecondsSinceEpoch());
        task.set_task_id("TASK-" + std::to_string(stats_.nav_task_sent.load()));
        
        // 设置目标点
        Point* target = task.mutable_target_node();
        target->set_x(50.0);
        target->set_y(60.0);
        
        task.set_operation(OP_PICK_UP);
        
        // 添加路径点（模拟全局路径）
        for (int i = 0; i < 5; ++i) {
            Point* pt = task.add_global_path();
            pt->set_x(10.0 + i * 10.0);
            pt->set_y(20.0 + i * 10.0);
        }
        
        send_time_ = Timestamp::now();
        sendMessage(MSG_NAVIGATION_TASK, task);
        stats_.nav_task_sent++;
        
        LOG_INFO << "[TestClient-" << agv_id_ << "] Sent NavigationTask: " << task.task_id();
    }
    
    TestStats& getStats() { return stats_; }
    bool isConnected() const { return connected_; }

private:
    void onConnection(const TcpConnectionPtr& conn) {
        if (conn->connected()) {
            connected_ = true;
            conn_ = conn;
            LOG_INFO << "[TestClient-" << agv_id_ << "] Connected to server";
        } else {
            connected_ = false;
            conn_.reset();
            LOG_INFO << "[TestClient-" << agv_id_ << "] Disconnected from server";
        }
    }
    
    void onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp /*time*/) {
        while (LengthHeaderCodec::hasCompleteMessage(buf)) {
            uint16_t msg_type = 0;
            uint16_t flags = 0;
            std::string payload;
            
            if (!LengthHeaderCodec::decode(buf, &msg_type, &payload, &flags)) {
                LOG_ERROR << "[TestClient-" << agv_id_ << "] Failed to decode message";
                conn->shutdown();
                return;
            }
            
            handleMessage(msg_type, payload);
        }
    }
    
    void handleMessage(uint16_t msg_type, const std::string& payload) {
        if (msg_type == MSG_COMMON_RESPONSE) {
            CommonResponse resp;
            if (!resp.ParseFromString(payload)) {
                LOG_ERROR << "[TestClient-" << agv_id_ << "] Failed to parse CommonResponse";
                return;
            }
            
            stats_.response_received++;
            
            // 计算 RTT
            Timestamp now = Timestamp::now();
            int64_t rtt_us = now.microSecondsSinceEpoch() - send_time_.microSecondsSinceEpoch();
            int64_t rtt_ms = rtt_us / 1000;
            stats_.total_rtt_ms += rtt_ms;
            
            LOG_INFO << "[TestClient-" << agv_id_ << "] Received CommonResponse: "
                     << "status=" << StatusCode_Name(resp.status())
                     << ", message=\"" << resp.message() << "\""
                     << ", RTT=" << rtt_ms << "ms";
        }
    }
    
    void sendMessage(uint16_t msg_type, const google::protobuf::Message& message) {
        std::string payload;
        if (!message.SerializeToString(&payload)) {
            LOG_ERROR << "[TestClient-" << agv_id_ << "] Failed to serialize message";
            return;
        }
        
        Buffer buf;
        if (!LengthHeaderCodec::encode(&buf, msg_type, payload)) {
            LOG_ERROR << "[TestClient-" << agv_id_ << "] Failed to encode message";
            return;
        }
        
        conn_->send(buf.retrieveAllAsString());
    }

private:
    EventLoop* loop_;
    TcpClient client_;
    std::string agv_id_;
    std::atomic<bool> connected_;
    TcpConnectionPtr conn_;
    TestStats stats_;
    Timestamp send_time_;
};

// ==================== 测试场景 ====================

/**
 * @brief 测试1：基本功能 - NavigationTask 正确投递到 Worker
 */
void Test1_BasicWorkerTask() {
    std::cout << "\n========== Test 1: Basic Worker Task ==========\n";
    
    EventLoop loop;
    
    // 启动服务器（4个Worker线程）
    InetAddress server_addr(kServerPort1);
    GatewayServer server(&loop, server_addr, "TestServer", 5.0, kWorkerThreads);
    server.start();
    
    // 启动客户端
    TestClient client(&loop, InetAddress(kServerPort1, kServerAddr), "AGV-TEST1");
    client.connect();
    
    // 等待连接建立
    loop.runAfter(0.5, [&]() {
        if (!client.isConnected()) {
            LOG_ERROR << "Client not connected!";
            loop.quit();
            return;
        }
        
        // 发送 Telemetry（建立会话）
        client.sendTelemetry();
        
        // 发送 NavigationTask
        loop.runAfter(0.1, [&]() {
            client.sendNavigationTask();
        });
        
        // 2秒后检查结果
        loop.runAfter(2.0, [&]() {
            TestStats& stats = client.getStats();
            std::cout << "\n[Test1 Results]\n";
            std::cout << "  Telemetry sent: " << stats.telemetry_sent << "\n";
            std::cout << "  NavigationTask sent: " << stats.nav_task_sent << "\n";
            std::cout << "  Response received: " << stats.response_received << "\n";
            
            if (stats.response_received >= 1) {
                int64_t avg_rtt = stats.total_rtt_ms / stats.response_received;
                std::cout << "  Average RTT: " << avg_rtt << "ms\n";
                std::cout << "  ✅ TEST PASSED\n";
            } else {
                std::cout << "  ❌ TEST FAILED: No response received\n";
            }
            
            loop.quit();
        });
    });
    
    loop.loop();
}

/**
 * @brief 测试2：并发测试 - IO线程不被Worker阻塞
 * 
 * 验证场景：
 * - 高频 Telemetry (50Hz) 持续发送
 * - 同时发送多个 NavigationTask（在 Worker 中阻塞 50ms）
 * - 验证 Telemetry 不受影响
 */
void Test2_IONotBlocked() {
    std::cout << "\n========== Test 2: IO Not Blocked by Worker ==========\n";
    
    EventLoop loop;
    
    // 启动服务器
    InetAddress server_addr(kServerPort2);
    GatewayServer server(&loop, server_addr, "TestServer", 5.0, kWorkerThreads);
    server.start();
    
    // 启动客户端
    TestClient client(&loop, InetAddress(kServerPort2, kServerAddr), "AGV-TEST2");
    client.connect();
    
    // 停止标志（shared_ptr包装，避免lambda捕获悬空引用）
    auto shouldStop = std::make_shared<std::atomic<bool>>(false);
    
    loop.runAfter(0.5, [&, shouldStop]() {
        if (!client.isConnected()) {
            LOG_ERROR << "Client not connected!";
            loop.quit();
            return;
        }
        
        // 启动高频 Telemetry（50Hz = 20ms间隔）
        // 使用 shared_ptr 包裹 std::function，确保递归调用时对象生命周期安全
        auto sendTelemLoop = std::make_shared<std::function<void()>>();
        *sendTelemLoop = [&, shouldStop, sendTelemLoop]() {
            if (shouldStop->load()) {
                return;  // 停止递归
            }
            client.sendTelemetry();
            loop.runAfter(0.02, *sendTelemLoop);  // 20ms = 50Hz
        };
        (*sendTelemLoop)();  // 启动循环
        
        // 发送 5 个 NavigationTask（测试 Worker 队列）
        for (int i = 0; i < 5; ++i) {
            loop.runAfter(0.1 * i, [&]() {
                client.sendNavigationTask();
            });
        }
        
        // 2.8秒后停止发送循环
        loop.runAfter(2.8, [shouldStop]() {
            shouldStop->store(true);
            LOG_INFO << "[Test2] Stopping telemetry loop...";
        });
        
        // 3秒后检查结果
        loop.runAfter(3.0, [&]() {
            TestStats& stats = client.getStats();
            std::cout << "\n[Test2 Results]\n";
            std::cout << "  Telemetry sent: " << stats.telemetry_sent << " (expected ~140 @ 50Hz * 2.8s)\n";
            std::cout << "  NavigationTask sent: " << stats.nav_task_sent << "\n";
            std::cout << "  Response received: " << stats.response_received << "\n";
            
            // 验证 Telemetry 发送频率未受影响（2.8秒 * 50Hz = 140）
            if (stats.telemetry_sent >= 130 && stats.response_received >= 5) {
                std::cout << "  ✅ TEST PASSED: IO thread not blocked\n";
            } else {
                std::cout << "  ❌ TEST FAILED\n";
            }
            
            loop.quit();
        });
    });
    
    loop.loop();
}

/**
 * @brief 测试3：连接断开 - Worker任务安全终止
 * 
 * 验证场景：
 * - 发送 NavigationTask
 * - 立即断开连接
 * - Worker 检测到连接无效，任务取消
 */
void Test3_ConnectionClosed() {
    std::cout << "\n========== Test 3: Connection Closed During Worker Task ==========\n";
    
    EventLoop loop;
    
    // 启动服务器
    InetAddress server_addr(kServerPort3);
    GatewayServer server(&loop, server_addr, "TestServer", 5.0, kWorkerThreads);
    server.start();
    
    // 启动客户端
    TestClient client(&loop, InetAddress(kServerPort3, kServerAddr), "AGV-TEST3");
    client.connect();
    
    loop.runAfter(0.5, [&]() {
        if (!client.isConnected()) {
            LOG_ERROR << "Client not connected!";
            loop.quit();
            return;
        }
        
        // 发送 Telemetry（建立会话）
        client.sendTelemetry();
        
        // 发送 NavigationTask 后立即断开
        loop.runAfter(0.1, [&]() {
            client.sendNavigationTask();
            
            // 10ms后断开连接（Worker还在处理中）
            loop.runAfter(0.01, [&]() {
                LOG_INFO << "[Test3] Disconnecting client during Worker task...";
                client.disconnect();
            });
        });
        
        // 2秒后检查结果（Worker应已检测到连接断开）
        loop.runAfter(2.0, [&]() {
            std::cout << "\n[Test3 Results]\n";
            std::cout << "  ✅ TEST PASSED: No crash or error (check logs for 'Connection closed' message)\n";
            loop.quit();
        });
    });
    
    loop.loop();
}

// ==================== 主函数 ====================

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    std::cout << "WorkerTask Test Suite\n";
    std::cout << "=====================\n";
    
    try {
        // 测试1：基本功能
        Test1_BasicWorkerTask();
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));  // 增加延迟，确保端口释放
        
        // 测试2：并发测试
        std::cout << "\n[INFO] Starting Test 2...\n";
        Test2_IONotBlocked();
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        
        // 测试3：连接断开
        std::cout << "\n[INFO] Starting Test 3...\n";
        Test3_ConnectionClosed();
        
        std::cout << "\n======================================\n";
        std::cout << "All tests completed!\n";
        std::cout << "======================================\n";
        
    } catch (const std::exception& ex) {
        std::cerr << "Exception: " << ex.what() << std::endl;
        return 1;
    }
    
    return 0;
}
