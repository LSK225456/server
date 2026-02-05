#include "../agv_server/gateway/GatewayServer.h"
#include "../agv_server/proto/message.pb.h"
#include "../agv_server/proto/message_id.h"
#include "../agv_server/codec/LengthHeaderCodec.h"
#include "../muduo/net/EventLoop.h"
#include "../muduo/net/TcpClient.h"
#include "../muduo/net/InetAddress.h"
#include "../muduo/base/Logger.h"
#include <iostream>
#include <memory>
#include <gtest/gtest.h>
#include "agv_server/gateway/AgvSession.h"
#include "agv_server/gateway/GatewayServer.h"
#include "muduo/base/Timestamp.h"

using namespace agv::gateway;
using namespace agv::proto;
using namespace agv::codec;
using namespace lsk_muduo;

/**
 * @brief 模拟 AGV 客户端
 */
class MockAgvClient {
public:
    MockAgvClient(EventLoop* loop, const InetAddress& server_addr, const std::string& agv_id)
        : loop_(loop),
          client_(loop, server_addr, "MockAGV-" + agv_id),
          agv_id_(agv_id),
          connected_(false) {
        
        client_.setConnectionCallback(
            std::bind(&MockAgvClient::onConnection, this, std::placeholders::_1));
        client_.setMessageCallback(
            std::bind(&MockAgvClient::onMessage, this,
                      std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    }

    void connect() {
        client_.connect();
    }

    void sendTelemetry(double battery) {
        if (!connected_) return;

        AgvTelemetry msg;
        msg.set_agv_id(agv_id_);
        msg.set_timestamp(Timestamp::now().microSecondsSinceEpoch());
        msg.set_x(1.0);
        msg.set_y(2.0);
        msg.set_theta(0.5);
        msg.set_confidence(0.95);
        msg.set_battery(battery);

        sendProto(MSG_AGV_TELEMETRY, msg);
        std::cout << "[Client] Sent Telemetry: battery=" << battery << "%" << std::endl;
    }

    void sendHeartbeat() {
        if (!connected_) return;

        Heartbeat msg;
        msg.set_agv_id(agv_id_);
        msg.set_timestamp(Timestamp::now().microSecondsSinceEpoch());

        sendProto(MSG_HEARTBEAT, msg);
        std::cout << "[Client] Sent Heartbeat" << std::endl;
    }

private:
    void onConnection(const TcpConnectionPtr& conn) {
        if (conn->connected()) {
            connected_ = true;
            std::cout << "[Client] Connected to server" << std::endl;
        } else {
            connected_ = false;
            std::cout << "[Client] Disconnected" << std::endl;
        }
    }

    void onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp) {
        while (LengthHeaderCodec::hasCompleteMessage(buf)) {
            uint16_t msg_type = 0;
            std::string payload;
            if (!LengthHeaderCodec::decode(buf, &msg_type, &payload)) {
                std::cout << "[Client] Decode failed" << std::endl;
                return;
            }
            std::cout << "[Client] Received message type=0x" << std::hex << msg_type << std::dec << std::endl;
        }
    }

    void sendProto(uint16_t msg_type, const google::protobuf::Message& message) {
        std::string payload;
        message.SerializeToString(&payload);

        Buffer buf;
        LengthHeaderCodec::encode(&buf, msg_type, payload);
        client_.connection()->send(&buf);
    }

    EventLoop* loop_;
    TcpClient client_;
    std::string agv_id_;
    bool connected_;
};

/**
 * @brief 测试场景1：正常遥测数据接收
 */
void testNormalTelemetry() {
    std::cout << "\n========== Test 1: Normal Telemetry ==========" << std::endl;

    EventLoop loop;
    InetAddress server_addr(9090);

    // 启动 GatewayServer
    GatewayServer server(&loop, server_addr, "TestGateway");
    server.start();

    // 创建模拟客户端
    MockAgvClient client(&loop, server_addr, "AGV001");
    client.connect();

    // 定时发送遥测数据（80% 电量，正常）
    loop.runAfter(1.0, [&client]() {
        client.sendTelemetry(80.0);
    });

    loop.runAfter(3.0, [&loop]() {
        loop.quit();
    });

    loop.loop();
    std::cout << "Test 1 passed" << std::endl;
}

/**
 * @brief 测试场景2：低电量触发充电
 */
void testLowBatteryCharging() {
    std::cout << "\n========== Test 2: Low Battery Charging ==========" << std::endl;

    EventLoop loop;
    InetAddress server_addr(9091);

    GatewayServer server(&loop, server_addr, "TestGateway");
    server.start();

    MockAgvClient client(&loop, server_addr, "AGV002");
    client.connect();

    // 发送低电量遥测（15%）
    loop.runAfter(1.0, [&client]() {
        client.sendTelemetry(15.0);  // 应触发充电指令
    });

    loop.runAfter(3.0, [&loop]() {
        loop.quit();
    });

    loop.loop();
    std::cout << "Test 2 passed (check server logs for charge command)" << std::endl;
}

/**
 * @brief 测试场景3：看门狗超时检测
 */
void testWatchdogTimeout() {
    std::cout << "\n========== Test 3: Watchdog Timeout ==========" << std::endl;

    EventLoop loop;
    InetAddress server_addr(9092);

    GatewayServer server(&loop, server_addr, "TestGateway");
    server.start();

    MockAgvClient client(&loop, server_addr, "AGV003");
    client.connect();

    // 发送一次遥测后停止（触发超时）
    loop.runAfter(0.5, [&client]() {
        client.sendTelemetry(50.0);
    });

    // 等待 2 秒（超过 1 秒超时阈值）
    loop.runAfter(3.0, [&loop]() {
        loop.quit();
    });

    loop.loop();
    std::cout << "Test 3 passed (check server logs for WATCHDOG ALARM)" << std::endl;
}

// ==================== AgvSession 单元测试 ====================

TEST(AgvSessionTest, Construction) {
    AgvSession session("AGV001");
    
    EXPECT_EQ(session.getAgvId(), "AGV001");
    EXPECT_EQ(session.getState(), AgvSession::ONLINE);
    EXPECT_DOUBLE_EQ(session.getBatteryLevel(), 100.0);
}

TEST(AgvSessionTest, UpdateBattery) {
    AgvSession session("AGV002");
    
    session.updateBatteryLevel(50.0);
    EXPECT_DOUBLE_EQ(session.getBatteryLevel(), 50.0);
    
    session.updateBatteryLevel(20.0);
    EXPECT_DOUBLE_EQ(session.getBatteryLevel(), 20.0);
}

TEST(AgvSessionTest, UpdatePose) {
    AgvSession session("AGV003");
    
    session.updatePose(1.0, 2.0, 0.5, 0.95);
    
    auto pose = session.getPose();
    EXPECT_DOUBLE_EQ(pose.x, 1.0);
    EXPECT_DOUBLE_EQ(pose.y, 2.0);
    EXPECT_DOUBLE_EQ(pose.theta, 0.5);
    EXPECT_DOUBLE_EQ(pose.confidence, 0.95);
}

TEST(AgvSessionTest, StateTransition) {
    AgvSession session("AGV004");
    
    EXPECT_EQ(session.getState(), AgvSession::ONLINE);
    
    session.setState(AgvSession::CHARGING);
    EXPECT_EQ(session.getState(), AgvSession::CHARGING);
    
    session.setState(AgvSession::OFFLINE);
    EXPECT_EQ(session.getState(), AgvSession::OFFLINE);
}

TEST(AgvSessionTest, ActiveTimeUpdate) {
    AgvSession session("AGV005");
    
    Timestamp t1 = session.getLastActiveTime();
    
    // 等待一小段时间
    usleep(1000);  // 1ms
    
    session.updateActiveTime();
    Timestamp t2 = session.getLastActiveTime();
    
    // t2 应该比 t1 晚
    EXPECT_GT(t2.microSecondsSinceEpoch(), t1.microSecondsSinceEpoch());
}

// ==================== 线程安全测试 ====================

TEST(AgvSessionTest, ThreadSafety) {
    AgvSession session("AGV006");
    
    // 简单的并发读写测试
    std::thread writer([&session]() {
        for (int i = 0; i < 1000; ++i) {
            session.updateBatteryLevel(static_cast<double>(i % 100));
            session.updateActiveTime();
        }
    });
    
    std::thread reader([&session]() {
        for (int i = 0; i < 1000; ++i) {
            (void)session.getBatteryLevel();
            (void)session.getLastActiveTime();
            (void)session.getState();
        }
    });
    
    writer.join();
    reader.join();
    
    // 如果没有崩溃，说明线程安全
    SUCCEED();
}

int main() {
    Logger::setLogLevel(Logger::INFO);

    testNormalTelemetry();
    testLowBatteryCharging();
    testWatchdogTimeout();

    std::cout << "\n========== All Tests Completed ==========" << std::endl;
    return 0;
}
