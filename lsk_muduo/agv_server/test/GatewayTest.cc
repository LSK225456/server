#include "../gateway/GatewayServer.h"
#include "../codec/LengthHeaderCodec.h"
#include "../proto/message.pb.h"
#include "../proto/common.pb.h"
#include "../proto/message_id.h"
#include "../../muduo/net/EventLoop.h"
#include "../../muduo/net/TcpClient.h"
#include "../../muduo/net/InetAddress.h"
#include "../../muduo/base/Logger.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

using namespace lsk_muduo;
using namespace agv::proto;
using namespace agv::codec;

/**
 * @brief GatewayServer 功能测试
 * 
 * 测试场景：
 * 1. 正常遥测上报（battery > 20%）
 * 2. 低电量触发充电指令（battery < 20%）
 * 3. 上行看门狗报警（1秒超时）
 */
class GatewayTestClient {
public:
    GatewayTestClient(EventLoop* loop, const InetAddress& server_addr, const std::string& agv_id)
        : loop_(loop),
          client_(loop, server_addr, "TestClient-" + agv_id),
          agv_id_(agv_id),
          connected_(false),
          battery_(100.0) {
        
        client_.setConnectionCallback(
            std::bind(&GatewayTestClient::onConnection, this, std::placeholders::_1));
        client_.setMessageCallback(
            std::bind(&GatewayTestClient::onMessage, this, 
                      std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    }

    void connect() {
        client_.connect();
    }

    void sendTelemetry(double battery, double x = 0.0, double y = 0.0) {
        if (!connected_) return;

        AgvTelemetry msg;
        msg.set_agv_id(agv_id_);
        msg.set_timestamp(Timestamp::now().microSecondsSinceEpoch());
        msg.set_x(x);
        msg.set_y(y);
        msg.set_theta(0.0);
        msg.set_confidence(0.95);
        msg.set_battery(battery);
        msg.set_linear_velocity(0.0);
        msg.set_angular_velocity(0.0);

        std::string payload;
        msg.SerializeToString(&payload);

        Buffer buf;
        LengthHeaderCodec::encode(&buf, MSG_AGV_TELEMETRY, payload);
        conn_->send(&buf);

        std::cout << "[SEND] Telemetry from [" << agv_id_ 
                  << "] battery=" << battery << "%" << std::endl;
    }

    void sendHeartbeat() {
        if (!connected_) return;

        Heartbeat msg;
        msg.set_agv_id(agv_id_);
        msg.set_timestamp(Timestamp::now().microSecondsSinceEpoch());

        std::string payload;
        msg.SerializeToString(&payload);

        Buffer buf;
        LengthHeaderCodec::encode(&buf, MSG_HEARTBEAT, payload);
        conn_->send(&buf);

        std::cout << "[SEND] Heartbeat from [" << agv_id_ << "]" << std::endl;
    }

private:
    void onConnection(const TcpConnectionPtr& conn) {
        if (conn->connected()) {
            std::cout << "[INFO] Client [" << agv_id_ << "] connected to server" << std::endl;
            connected_ = true;
            conn_ = conn;
        } else {
            std::cout << "[INFO] Client [" << agv_id_ << "] disconnected" << std::endl;
            connected_ = false;
        }
    }

    void onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp time) {
        (void)time;
        while (LengthHeaderCodec::hasCompleteMessage(buf)) {
            uint16_t msg_type = 0;
            uint16_t flags = 0;
            std::string payload;

            if (!LengthHeaderCodec::decode(buf, &msg_type, &payload, &flags)) {
                std::cerr << "[ERROR] Failed to decode message" << std::endl;
                return;
            }

            if (msg_type == MSG_AGV_COMMAND) {
                AgvCommand cmd;
                if (cmd.ParseFromString(payload)) {
                    std::cout << "[RECV] AgvCommand from server: cmd_type=" 
                              << cmd.cmd_type() << std::endl;
                }
            }
        }
    }

    EventLoop* loop_;
    TcpClient client_;
    std::string agv_id_;
    bool connected_;
    TcpConnectionPtr conn_;
    double battery_;
};

// ========== 测试场景 ==========

void test_scenario_normal(EventLoop* loop, const InetAddress& server_addr) {
    std::cout << "\n========== Test 1: Normal Operation (battery > 20%) ==========" << std::endl;
    
    GatewayTestClient client(loop, server_addr, "AGV-001");
    client.connect();

    loop->runAfter(1.0, [&]() {
        std::cout << "\n[INFO] Sending telemetry with battery=50%" << std::endl;
        client.sendTelemetry(50.0, 1.0, 2.0);
    });

    loop->runAfter(2.0, [&]() {
        client.sendHeartbeat();
    });

    loop->runAfter(3.0, [loop]() {
        std::cout << "\n[INFO] Test 1 completed" << std::endl;
        loop->quit();
    });
}

void test_scenario_low_battery(EventLoop* loop, const InetAddress& server_addr) {
    std::cout << "\n========== Test 2: Low Battery Trigger (battery < 20%) ==========" << std::endl;
    
    GatewayTestClient client(loop, server_addr, "AGV-002");
    client.connect();

    loop->runAfter(1.0, [&]() {
        std::cout << "\n[INFO] Sending telemetry with battery=15% (should trigger charge)" << std::endl;
        client.sendTelemetry(15.0, 3.0, 4.0);
    });

    loop->runAfter(3.0, [loop]() {
        std::cout << "\n[INFO] Test 2 completed (check if charge command received)" << std::endl;
        loop->quit();
    });
}

void test_scenario_watchdog(EventLoop* loop, const InetAddress& server_addr) {
    std::cout << "\n========== Test 3: Watchdog Timeout (1s silence) ==========" << std::endl;
    
    GatewayTestClient client(loop, server_addr, "AGV-003");
    client.connect();

    loop->runAfter(1.0, [&]() {
        std::cout << "\n[INFO] Sending initial telemetry" << std::endl;
        client.sendTelemetry(80.0);
    });

    loop->runAfter(1.5, []() {
        std::cout << "\n[INFO] Now entering 1.5s silence (watchdog should alarm)..." << std::endl;
    });

    loop->runAfter(4.0, [loop]() {
        std::cout << "\n[INFO] Test 3 completed (check server logs for WATCHDOG ALARM)" << std::endl;
        loop->quit();
    });
}

// ========== 主程序 ==========

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " [test_num]" << std::endl;
        std::cerr << "  test_num = 1: Normal operation" << std::endl;
        std::cerr << "  test_num = 2: Low battery trigger" << std::endl;
        std::cerr << "  test_num = 3: Watchdog timeout" << std::endl;
        return 1;
    }

    int test_num = std::atoi(argv[1]);
    InetAddress server_addr("127.0.0.1", 9090);

    EventLoop loop;

    switch (test_num) {
        case 1:
            test_scenario_normal(&loop, server_addr);
            break;
        case 2:
            test_scenario_low_battery(&loop, server_addr);
            break;
        case 3:
            test_scenario_watchdog(&loop, server_addr);
            break;
        default:
            std::cerr << "Invalid test number: " << test_num << std::endl;
            return 1;
    }

    loop.loop();
    return 0;
}
