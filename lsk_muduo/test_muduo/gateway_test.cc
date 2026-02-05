#include "../agv_server/gateway/GatewayServer.h"
#include "../agv_server/gateway/AgvSession.h"
#include "../muduo/net/EventLoop.h"
#include "../muduo/net/InetAddress.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>

using namespace agv::gateway;

/**
 * @brief GatewayServer 单元测试（迭代一：Day 1-2）
 * @note 测试场景：
 *       1. AgvSession 基本功能（构造、Getter/Setter、线程安全）
 *       2. GatewayServer 启动与连接
 *       3. 看门狗超时检测（需模拟客户端）
 *       4. 低电量触发充电（需模拟客户端）
 */

// ==================== AgvSession 单元测试 ====================

/**
 * @brief 测试：AgvSession 基本操作
 */
TEST(AgvSessionTest, BasicOperations) {
    AgvSession session("AGV-001");
    
    // 验证默认值
    EXPECT_EQ(session.getAgvId(), "AGV-001");
    EXPECT_EQ(session.getState(), AgvSession::ONLINE);
    EXPECT_DOUBLE_EQ(session.getBatteryLevel(), 100.0);
    
    // 更新电量
    session.updateBatteryLevel(50.0);
    EXPECT_DOUBLE_EQ(session.getBatteryLevel(), 50.0);
    
    // 更新状态
    session.setState(AgvSession::CHARGING);
    EXPECT_EQ(session.getState(), AgvSession::CHARGING);
    
    // 更新位姿
    session.updatePose(1.5, 2.5, 90.0, 0.95);
    auto pose = session.getPose();
    EXPECT_DOUBLE_EQ(pose.x, 1.5);
    EXPECT_DOUBLE_EQ(pose.y, 2.5);
    EXPECT_DOUBLE_EQ(pose.theta, 90.0);
    EXPECT_DOUBLE_EQ(pose.confidence, 0.95);
}

/**
 * @brief 测试：线程安全（并发读写）
 */
TEST(AgvSessionTest, ThreadSafety) {
    AgvSession session("AGV-002");
    
    // 启动写线程
    std::thread writer([&session]() {
        for (int i = 0; i < 1000; ++i) {
            session.updateBatteryLevel(static_cast<double>(i % 100));
            session.updateActiveTime();
        }
    });
    
    // 启动读线程
    std::thread reader([&session]() {
        for (int i = 0; i < 1000; ++i) {
            double level = session.getBatteryLevel();
            (void)level;
            Timestamp ts = session.getLastActiveTime();
            (void)ts;
        }
    });
    
    writer.join();
    reader.join();
    
    // 验证最终状态一致
    EXPECT_GE(session.getBatteryLevel(), 0.0);
    EXPECT_LE(session.getBatteryLevel(), 100.0);
}

// ==================== GatewayServer 集成测试 ====================

/**
 * @brief 测试夹具：GatewayServer
 */
class GatewayServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 在后台线程启动服务器
        server_thread_ = std::thread([this]() {
            EventLoop loop;
            loop_ = &loop;
            
            InetAddress listen_addr(19090);  // 使用测试端口
            server_ = new GatewayServer(&loop, listen_addr, "TestGateway");
            server_->start();
            
            loop.loop();
        });
        
        // 等待服务器启动
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    
    void TearDown() override {
        if (loop_) {
            loop_->quit();
        }
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        delete server_;
    }
    
    EventLoop* loop_ = nullptr;
    GatewayServer* server_ = nullptr;
    std::thread server_thread_;
};

/**
 * @brief 测试：服务器启动
 */
TEST_F(GatewayServerTest, ServerStartup) {
    EXPECT_NE(server_, nullptr);
    EXPECT_NE(loop_, nullptr);
    
    // 等待 1 秒，验证服务器正常运行
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

// ==================== 主函数 ====================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
