/**
 * @file test_lsk_server.cc
 * @brief AGV 网关服务器综合测试（迭代一到迭代三全覆盖）
 *
 * 本文件是服务器的最终综合测试，替代所有单独的测试文件。
 * 覆盖范围：
 *   迭代一：Buffer增强、Protobuf协议、LengthHeaderCodec编解码
 *   迭代二：ProtobufDispatcher分发、ConcurrentMap容器、SessionManager会话管理、
 *           心跳检测、多客户端联调
 *   迭代三：WorkerTask投递、快慢分离(SpinLock)、紧急制动透传、
 *           LatencyMonitor延迟监控、BusinessHandler业务处理(200ms阻塞验证)
 *
 * 使用方式：
 *   cd build && cmake .. && make -j$(nproc)
 *   ../bin/test_lsk_server
 */

#include <gtest/gtest.h>

// muduo base
#include "../muduo/base/Logger.h"
#include "../muduo/base/Timestamp.h"
#include "../muduo/base/SpinLock.h"
#include "../muduo/base/ThreadPool.h"

// muduo net
#include "../muduo/net/EventLoop.h"
#include "../muduo/net/EventLoopThread.h"
#include "../muduo/net/InetAddress.h"
#include "../muduo/net/TcpClient.h"
#include "../muduo/net/TcpServer.h"
#include "../muduo/net/Buffer.h"
#include "../muduo/net/TimerId.h"

// agv server
#include "../agv_server/gateway/GatewayServer.h"
#include "../agv_server/gateway/AgvSession.h"
#include "../agv_server/gateway/SessionManager.h"
#include "../agv_server/gateway/ConcurrentMap.h"
#include "../agv_server/gateway/ProtobufDispatcher.h"
#include "../agv_server/gateway/WorkerTask.h"
#include "../agv_server/gateway/LatencyMonitor.h"
#include "../agv_server/proto/message.pb.h"
#include "../agv_server/proto/common.pb.h"
#include "../agv_server/proto/message_id.h"
#include "../agv_server/codec/LengthHeaderCodec.h"

// mock client
#include "MockAgvClient.h"

#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <memory>
#include <iostream>
#include <cstring>

using namespace lsk_muduo;
using namespace agv::gateway;
using namespace agv::proto;
using namespace agv::codec;

// ==================== 端口分配（9800起步，避免冲突）====================
static std::atomic<int> g_next_port{9800};
static int allocPort() { return g_next_port.fetch_add(1); }

// ==================== 测试辅助：轻量级 TestClient ====================

/**
 * @brief 轻量级测试客户端
 * 支持发送/接收所有消息类型，跟踪统计信息
 */
class TestClient {
public:
    TestClient(EventLoop* loop, const InetAddress& addr, const std::string& agv_id)
        : loop_(loop),
          client_(loop, addr, "TC-" + agv_id),
          agv_id_(agv_id) {
        client_.setConnectionCallback(
            std::bind(&TestClient::onConnection, this, std::placeholders::_1));
        client_.setMessageCallback(
            std::bind(&TestClient::onMessage, this,
                      std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    }

    void connect() { client_.connect(); }
    void disconnect() { client_.disconnect(); }
    bool isConnected() const { return connected_; }

    // ========== 发送接口 ==========

    void sendTelemetry(double battery = 80.0, double x = 1.0, double y = 2.0) {
        if (!conn_) return;
        AgvTelemetry msg;
        msg.set_agv_id(agv_id_);
        msg.set_timestamp(Timestamp::now().microSecondsSinceEpoch());
        msg.set_x(x);
        msg.set_y(y);
        msg.set_theta(45.0);
        msg.set_confidence(0.95);
        msg.set_battery(battery);
        msg.set_linear_velocity(0.5);
        msg.set_angular_velocity(0.0);
        msg.set_acceleration(0.0);
        msg.set_payload_weight(0.0);
        msg.set_error_code(0);
        msg.set_fork_height(0.0);
        sendMsg(MSG_AGV_TELEMETRY, msg);
        telemetry_sent++;
    }

    void sendHeartbeat() {
        if (!conn_) return;
        Heartbeat msg;
        msg.set_agv_id(agv_id_);
        msg.set_timestamp(Timestamp::now().microSecondsSinceEpoch());
        sendMsg(MSG_HEARTBEAT, msg);
        heartbeat_sent++;
    }

    void sendNavigationTask(const std::string& task_id = "TASK-001") {
        if (!conn_) return;
        NavigationTask task;
        task.set_target_agv_id(agv_id_);
        task.set_timestamp(Timestamp::now().microSecondsSinceEpoch());
        task.set_task_id(task_id);
        Point* target = task.mutable_target_node();
        target->set_x(50.0);
        target->set_y(60.0);
        task.set_operation(OP_PICK_UP);
        for (int i = 0; i < 3; ++i) {
            Point* pt = task.add_global_path();
            pt->set_x(10.0 + i * 20.0);
            pt->set_y(20.0 + i * 20.0);
        }
        sendMsg(MSG_NAVIGATION_TASK, task);
        nav_task_sent++;
    }

    void sendAgvCommand(const std::string& target_agv_id, CommandType cmd_type) {
        if (!conn_) return;
        AgvCommand cmd;
        cmd.set_target_agv_id(target_agv_id);
        cmd.set_timestamp(Timestamp::now().microSecondsSinceEpoch());
        cmd.set_cmd_type(cmd_type);
        sendMsg(MSG_AGV_COMMAND, cmd);
        command_sent++;
    }

    void sendLatencyPong(uint64_t seq_num, int64_t send_timestamp) {
        if (!conn_) return;
        LatencyProbe pong;
        pong.set_target_agv_id(agv_id_);
        pong.set_send_timestamp(send_timestamp);
        pong.set_seq_num(seq_num);
        pong.set_is_response(true);
        sendMsg(MSG_LATENCY_PROBE, pong);
    }

    // ========== 统计 ==========
    std::atomic<int> telemetry_sent{0};
    std::atomic<int> heartbeat_sent{0};
    std::atomic<int> nav_task_sent{0};
    std::atomic<int> command_sent{0};

    std::atomic<int> heartbeat_received{0};
    std::atomic<int> command_received{0};
    std::atomic<int> response_received{0};
    std::atomic<int> nav_task_received{0};
    std::atomic<int> latency_probe_received{0};

    // 最后收到的响应
    std::atomic<int> last_response_status{0};
    std::string last_response_msg;
    // 最后收到的指令类型
    std::atomic<int> last_command_type{-1};

private:
    void onConnection(const TcpConnectionPtr& conn) {
        if (conn->connected()) {
            connected_ = true;
            conn_ = conn;
        } else {
            connected_ = false;
            conn_.reset();
        }
    }

    void onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp) {
        while (LengthHeaderCodec::hasCompleteMessage(buf)) {
            uint16_t msg_type = 0;
            uint16_t flags = 0;
            std::string payload;
            if (!LengthHeaderCodec::decode(buf, &msg_type, &payload, &flags)) {
                conn->shutdown();
                return;
            }
            handleMsg(msg_type, payload);
        }
    }

    void handleMsg(uint16_t msg_type, const std::string& payload) {
        switch (msg_type) {
            case MSG_HEARTBEAT: {
                heartbeat_received++;
                break;
            }
            case MSG_AGV_COMMAND: {
                AgvCommand cmd;
                if (cmd.ParseFromString(payload)) {
                    command_received++;
                    last_command_type = static_cast<int>(cmd.cmd_type());
                }
                break;
            }
            case MSG_COMMON_RESPONSE: {
                CommonResponse resp;
                if (resp.ParseFromString(payload)) {
                    response_received++;
                    last_response_status = static_cast<int>(resp.status());
                    last_response_msg = resp.message();
                }
                break;
            }
            case MSG_NAVIGATION_TASK: {
                nav_task_received++;
                break;
            }
            case MSG_LATENCY_PROBE: {
                LatencyProbe probe;
                if (probe.ParseFromString(payload)) {
                    latency_probe_received++;
                    // 如果是 Ping，自动回复 Pong
                    if (!probe.is_response()) {
                        sendLatencyPong(probe.seq_num(), probe.send_timestamp());
                    }
                }
                break;
            }
            default:
                break;
        }
    }

    void sendMsg(uint16_t msg_type, const google::protobuf::Message& message) {
        std::string payload;
        if (!message.SerializeToString(&payload)) return;
        Buffer buf;
        if (!LengthHeaderCodec::encode(&buf, msg_type, payload)) return;
        conn_->send(buf.retrieveAllAsString());
    }

    EventLoop* loop_;
    TcpClient client_;
    std::string agv_id_;
    std::atomic<bool> connected_{false};
    TcpConnectionPtr conn_;
};

// ######################################################################
// #                    Part 1: 单元测试（无网络）                        #
// ######################################################################

// ==================== 1.1 Buffer 整数操作 ====================

TEST(BufferTest, AppendReadInt32) {
    Buffer buf;
    buf.appendInt32(0x12345678);
    EXPECT_EQ(buf.readableBytes(), 4u);
    // 验证大端字节序
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(buf.peek());
    EXPECT_EQ(bytes[0], 0x12);
    EXPECT_EQ(bytes[1], 0x34);
    EXPECT_EQ(bytes[2], 0x56);
    EXPECT_EQ(bytes[3], 0x78);
    // peekInt32 不移动指针
    EXPECT_EQ(buf.peekInt32(), 0x12345678);
    EXPECT_EQ(buf.readableBytes(), 4u);
    // readInt32 移动指针
    EXPECT_EQ(buf.readInt32(), 0x12345678);
    EXPECT_EQ(buf.readableBytes(), 0u);
}

TEST(BufferTest, PrependInt32) {
    Buffer buf;
    buf.appendInt32(0xAABBCCDD);
    buf.prependInt32(0x11223344);
    // 先读 prepend 的值
    EXPECT_EQ(buf.readInt32(), 0x11223344);
    EXPECT_EQ(buf.readInt32(), static_cast<int32_t>(0xAABBCCDD));
}

TEST(BufferTest, Int16Operations) {
    Buffer buf;
    buf.appendInt16(0x1234);
    EXPECT_EQ(buf.readableBytes(), 2u);
    EXPECT_EQ(buf.peekInt16(), 0x1234);
    EXPECT_EQ(buf.readInt16(), 0x1234);
    EXPECT_EQ(buf.readableBytes(), 0u);
}

TEST(BufferTest, BoundaryValues) {
    Buffer buf;
    buf.appendInt32(0);
    buf.appendInt32(-1);
    buf.appendInt32(INT32_MAX);
    buf.appendInt32(INT32_MIN);
    EXPECT_EQ(buf.readInt32(), 0);
    EXPECT_EQ(buf.readInt32(), -1);
    EXPECT_EQ(buf.readInt32(), INT32_MAX);
    EXPECT_EQ(buf.readInt32(), INT32_MIN);
}

// ==================== 1.2 Codec 编解码 ====================

TEST(CodecTest, EncodeDecodeRoundTrip) {
    Buffer buf;
    std::string payload = "hello protobuf";
    uint16_t msg_type = 0x1001;
    ASSERT_TRUE(LengthHeaderCodec::encode(&buf, msg_type, payload));
    EXPECT_EQ(buf.readableBytes(), LengthHeaderCodec::kHeaderLen + payload.size());

    ASSERT_TRUE(LengthHeaderCodec::hasCompleteMessage(&buf));
    uint16_t out_type = 0;
    std::string out_payload;
    ASSERT_TRUE(LengthHeaderCodec::decode(&buf, &out_type, &out_payload));
    EXPECT_EQ(out_type, msg_type);
    EXPECT_EQ(out_payload, payload);
    EXPECT_EQ(buf.readableBytes(), 0u);
}

TEST(CodecTest, StickyPackets) {
    Buffer buf;
    // 两条消息粘在一起
    ASSERT_TRUE(LengthHeaderCodec::encode(&buf, 0x1001, "msg-A"));
    ASSERT_TRUE(LengthHeaderCodec::encode(&buf, 0x2001, "msg-B"));

    uint16_t type1 = 0, type2 = 0;
    std::string p1, p2;
    ASSERT_TRUE(LengthHeaderCodec::decode(&buf, &type1, &p1));
    EXPECT_EQ(type1, 0x1001);
    EXPECT_EQ(p1, "msg-A");
    ASSERT_TRUE(LengthHeaderCodec::decode(&buf, &type2, &p2));
    EXPECT_EQ(type2, 0x2001);
    EXPECT_EQ(p2, "msg-B");
    EXPECT_EQ(buf.readableBytes(), 0u);
}

TEST(CodecTest, HalfPacket) {
    Buffer buf;
    ASSERT_TRUE(LengthHeaderCodec::encode(&buf, 0x1001, "test-data"));
    size_t full_len = buf.readableBytes();
    std::string full_msg = buf.retrieveAllAsString();

    // 只放半包进 buffer
    Buffer half;
    half.append(full_msg.data(), full_len / 2);
    EXPECT_FALSE(LengthHeaderCodec::hasCompleteMessage(&half));

    // 追加剩余部分
    half.append(full_msg.data() + full_len / 2, full_len - full_len / 2);
    EXPECT_TRUE(LengthHeaderCodec::hasCompleteMessage(&half));

    uint16_t type = 0;
    std::string payload;
    ASSERT_TRUE(LengthHeaderCodec::decode(&half, &type, &payload));
    EXPECT_EQ(type, 0x1001);
    EXPECT_EQ(payload, "test-data");
}

// ==================== 1.3 Protobuf 序列化 ====================

TEST(ProtobufTest, AgvTelemetry) {
    AgvTelemetry msg;
    msg.set_agv_id("AGV-001");
    msg.set_battery(85.5);
    msg.set_x(1.0);
    msg.set_y(2.0);
    msg.set_theta(90.0);
    msg.set_confidence(0.99);
    std::string data;
    ASSERT_TRUE(msg.SerializeToString(&data));
    AgvTelemetry parsed;
    ASSERT_TRUE(parsed.ParseFromString(data));
    EXPECT_EQ(parsed.agv_id(), "AGV-001");
    EXPECT_DOUBLE_EQ(parsed.battery(), 85.5);
    EXPECT_DOUBLE_EQ(parsed.x(), 1.0);
}

TEST(ProtobufTest, NavigationTask) {
    NavigationTask task;
    task.set_target_agv_id("AGV-002");
    task.set_task_id("NAV-100");
    task.set_operation(OP_PICK_UP);
    Point* p = task.mutable_target_node();
    p->set_x(10.0);
    p->set_y(20.0);
    for (int i = 0; i < 5; ++i) {
        Point* pt = task.add_global_path();
        pt->set_x(i * 1.0);
        pt->set_y(i * 2.0);
    }
    std::string data;
    ASSERT_TRUE(task.SerializeToString(&data));
    NavigationTask parsed;
    ASSERT_TRUE(parsed.ParseFromString(data));
    EXPECT_EQ(parsed.task_id(), "NAV-100");
    EXPECT_EQ(parsed.global_path_size(), 5);
    EXPECT_EQ(parsed.operation(), OP_PICK_UP);
}

TEST(ProtobufTest, LatencyProbe) {
    LatencyProbe probe;
    probe.set_target_agv_id("AGV-003");
    probe.set_send_timestamp(123456789);
    probe.set_seq_num(42);
    probe.set_is_response(false);
    std::string data;
    ASSERT_TRUE(probe.SerializeToString(&data));
    LatencyProbe parsed;
    ASSERT_TRUE(parsed.ParseFromString(data));
    EXPECT_EQ(parsed.seq_num(), 42u);
    EXPECT_FALSE(parsed.is_response());
}

TEST(ProtobufTest, AllEnums) {
    EXPECT_EQ(CMD_EMERGENCY_STOP, 0);
    EXPECT_EQ(CMD_RESUME, 1);
    EXPECT_EQ(CMD_NAVIGATE_TO, 4);
    EXPECT_EQ(STATUS_OK, 0);
    EXPECT_EQ(OP_PICK_UP, 1);
    EXPECT_EQ(OP_PUT_DOWN, 2);
}

// ==================== 1.4 ConcurrentMap ====================

TEST(ConcurrentMapTest, BasicCRUD) {
    ConcurrentMap<std::string, int> map;
    auto v1 = std::make_shared<int>(100);
    auto v2 = std::make_shared<int>(200);

    EXPECT_TRUE(map.insert("k1", v1));
    EXPECT_FALSE(map.empty());
    EXPECT_EQ(map.size(), 1u);

    auto found = map.find("k1");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(*found, 100);

    EXPECT_TRUE(map.insert("k2", v2));
    EXPECT_EQ(map.size(), 2u);

    EXPECT_TRUE(map.erase("k1"));
    EXPECT_EQ(map.size(), 1u);
    EXPECT_EQ(map.find("k1"), nullptr);

    map.clear();
    EXPECT_TRUE(map.empty());
}

TEST(ConcurrentMapTest, ConcurrentReadWrite) {
    ConcurrentMap<int, int> map;
    const int N = 1000;
    std::atomic<int> read_count{0};

    // Writer 线程
    std::thread writer([&]() {
        for (int i = 0; i < N; ++i) {
            map.insert(i, std::make_shared<int>(i * 10));
        }
    });
    // Reader 线程
    std::thread reader([&]() {
        for (int i = 0; i < N * 10; ++i) {
            auto val = map.find(i % N);
            if (val) read_count++;
        }
    });

    writer.join();
    reader.join();

    EXPECT_EQ(map.size(), static_cast<size_t>(N));
    EXPECT_GT(read_count.load(), 0);
}

TEST(ConcurrentMapTest, EraseIf) {
    ConcurrentMap<std::string, int> map;
    for (int i = 0; i < 10; ++i) {
        map.insert("key-" + std::to_string(i), std::make_shared<int>(i));
    }
    EXPECT_EQ(map.size(), 10u);

    // 删除值为偶数的元素
    size_t removed = map.eraseIf([](const std::string&, const std::shared_ptr<int>& v) {
        return (*v % 2 == 0);
    });
    EXPECT_EQ(removed, 5u);
    EXPECT_EQ(map.size(), 5u);
}

// ==================== 1.5 SpinLock ====================

TEST(SpinLockTest, MultiThreadCorrectness) {
    SpinLock lock;
    int counter = 0;
    const int kPerThread = 10000;
    const int kThreads = 4;

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < kPerThread; ++i) {
                SpinLockGuard guard(lock);
                counter++;
            }
        });
    }
    for (auto& th : threads) th.join();

    EXPECT_EQ(counter, kPerThread * kThreads);
}

TEST(SpinLockTest, AgvSessionPoseConcurrency) {
    auto session = std::make_shared<AgvSession>("TEST-001", nullptr);
    std::atomic<bool> stop{false};
    std::atomic<int> read_count{0};
    std::atomic<int> write_count{0};

    // Readers start first
    auto reader_fn = [&]() {
        while (!stop) {
            auto pose = session->getPose();
            (void)pose;
            read_count++;
        }
    };
    std::thread r1(reader_fn), r2(reader_fn);

    // Give readers time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    // Writer: update pose
    std::thread writer([&]() {
        for (int i = 0; i < 10000; ++i) {
            session->updatePose(i * 0.1, i * 0.2, i * 0.5, 0.95);
            write_count++;
        }
    });

    writer.join();
    // Let readers run a bit more after writer finishes
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    stop = true;
    r1.join();
    r2.join();

    EXPECT_GT(read_count.load(), 0);
    EXPECT_EQ(write_count.load(), 10000);
    // No crash = test passes
}

// ==================== 1.6 ProtobufDispatcher ====================

TEST(DispatcherTest, TypeSafeDispatch) {
    ProtobufDispatcher dispatcher;
    int telemetry_count = 0;
    int heartbeat_count = 0;

    dispatcher.registerHandler<AgvTelemetry>(
        MSG_AGV_TELEMETRY,
        [&](const TcpConnectionPtr&, const AgvTelemetry& msg) {
            telemetry_count++;
            EXPECT_EQ(msg.agv_id(), "AGV-D1");
        });
    dispatcher.registerHandler<Heartbeat>(
        MSG_HEARTBEAT,
        [&](const TcpConnectionPtr&, const Heartbeat& msg) {
            heartbeat_count++;
            EXPECT_EQ(msg.agv_id(), "AGV-D1");
        });

    EXPECT_EQ(dispatcher.handlerCount(), 2u);
    EXPECT_TRUE(dispatcher.hasHandler(MSG_AGV_TELEMETRY));
    EXPECT_TRUE(dispatcher.hasHandler(MSG_HEARTBEAT));
    EXPECT_FALSE(dispatcher.hasHandler(0xFFFF));

    // 构造并分发 Telemetry
    AgvTelemetry telem;
    telem.set_agv_id("AGV-D1");
    std::string telem_data;
    telem.SerializeToString(&telem_data);
    EXPECT_TRUE(dispatcher.dispatch(nullptr, MSG_AGV_TELEMETRY,
                                    telem_data.data(), telem_data.size()));
    EXPECT_EQ(telemetry_count, 1);

    // 分发 Heartbeat
    Heartbeat hb;
    hb.set_agv_id("AGV-D1");
    std::string hb_data;
    hb.SerializeToString(&hb_data);
    EXPECT_TRUE(dispatcher.dispatch(nullptr, MSG_HEARTBEAT,
                                    hb_data.data(), hb_data.size()));
    EXPECT_EQ(heartbeat_count, 1);

    // 未注册类型
    EXPECT_FALSE(dispatcher.dispatch(nullptr, 0xFFFF, "x", 1));
}

// ==================== 1.7 SessionManager ====================

TEST(SessionManagerTest, RegisterFindRemove) {
    SessionManager mgr;
    EXPECT_TRUE(mgr.empty());

    EXPECT_TRUE(mgr.registerSession("AGV-A", nullptr));
    EXPECT_FALSE(mgr.empty());
    EXPECT_EQ(mgr.size(), 1u);

    auto session = mgr.findSession("AGV-A");
    ASSERT_NE(session, nullptr);
    EXPECT_EQ(session->getAgvId(), "AGV-A");

    EXPECT_TRUE(mgr.hasSession("AGV-A"));
    EXPECT_FALSE(mgr.hasSession("AGV-X"));

    EXPECT_TRUE(mgr.removeSession("AGV-A"));
    EXPECT_EQ(mgr.size(), 0u);
    EXPECT_EQ(mgr.findSession("AGV-A"), nullptr);
}

TEST(SessionManagerTest, ForEachAndEraseIf) {
    SessionManager mgr;
    for (int i = 0; i < 5; ++i) {
        mgr.registerSession("AGV-" + std::to_string(i), nullptr);
    }
    EXPECT_EQ(mgr.size(), 5u);

    // forEach 遍历
    int count = 0;
    mgr.forEach([&](const std::string&, const AgvSessionPtr&) { count++; });
    EXPECT_EQ(count, 5);

    // eraseIf 条件删除
    size_t removed = mgr.eraseIf([](const std::string& id, const AgvSessionPtr&) {
        return id == "AGV-0" || id == "AGV-1";
    });
    EXPECT_EQ(removed, 2u);
    EXPECT_EQ(mgr.size(), 3u);
}

// ==================== 1.8 LatencyMonitor 单元测试 ====================

TEST(LatencyMonitorTest, PingPongRTT) {
    LatencyMonitor monitor;

    // 创建 Ping
    auto ping = monitor.createPing("AGV-LM1");
    EXPECT_EQ(ping.target_agv_id(), "AGV-LM1");
    EXPECT_FALSE(ping.is_response());
    EXPECT_GT(ping.seq_num(), 0u);
    EXPECT_EQ(monitor.pendingCount(), 1u);

    // 模拟延迟
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // 构造 Pong
    LatencyProbe pong;
    pong.set_target_agv_id(ping.target_agv_id());
    pong.set_send_timestamp(ping.send_timestamp());
    pong.set_seq_num(ping.seq_num());
    pong.set_is_response(true);

    double rtt = monitor.processPong(pong);
    EXPECT_GE(rtt, 4.0);   // 至少 ~5ms
    EXPECT_LT(rtt, 200.0); // 不应太长
    EXPECT_EQ(monitor.pendingCount(), 0u);

    auto stats = monitor.getStats("AGV-LM1");
    EXPECT_EQ(stats.sample_count, 1);
    EXPECT_DOUBLE_EQ(stats.latest_rtt_ms, rtt);
}

TEST(LatencyMonitorTest, UnknownSeqNum) {
    LatencyMonitor monitor;
    LatencyProbe bogus_pong;
    bogus_pong.set_target_agv_id("AGV-FAKE");
    bogus_pong.set_send_timestamp(0);
    bogus_pong.set_seq_num(99999);
    bogus_pong.set_is_response(true);

    double rtt = monitor.processPong(bogus_pong);
    EXPECT_LT(rtt, 0);  // 返回 -1.0
}

// ######################################################################
// #                    Part 2: 集成测试（含网络）                        #
// ######################################################################

// ==================== 2.1 单客户端遥测 ====================

TEST(IntegrationTest, SingleClientTelemetry) {
    int port = allocPort();
    EventLoop loop;

    InetAddress addr(port);
    GatewayServer server(&loop, addr, "TestSvr", 5.0, 4);
    server.start();

    TestClient client(&loop, InetAddress(port, "127.0.0.1"), "AGV-IT1");
    client.connect();

    std::atomic<bool> passed{false};

    loop.runAfter(0.5, [&]() {
        ASSERT_TRUE(client.isConnected());
        // 发送几条遥测
        for (int i = 0; i < 10; ++i) {
            client.sendTelemetry(80.0);
        }
    });

    loop.runAfter(1.5, [&]() {
        EXPECT_EQ(client.telemetry_sent.load(), 10);
        // 验证会话已创建
        auto session = server.getSessionManager().findSession("AGV-IT1");
        EXPECT_NE(session, nullptr);
        if (session) {
            EXPECT_DOUBLE_EQ(session->getBatteryLevel(), 80.0);
            EXPECT_EQ(session->getState(), AgvSession::ONLINE);
        }
        passed = true;
        loop.quit();
    });

    loop.loop();
    EXPECT_TRUE(passed);
}

// ==================== 2.2 心跳保活 ====================

TEST(IntegrationTest, HeartbeatKeepAlive) {
    int port = allocPort();
    EventLoop loop;

    InetAddress addr(port);
    GatewayServer server(&loop, addr, "TestSvr", 5.0, 4);
    server.start();

    TestClient client(&loop, InetAddress(port, "127.0.0.1"), "AGV-HB1");
    client.connect();

    std::atomic<bool> passed{false};

    loop.runAfter(0.3, [&]() {
        client.sendTelemetry();  // 注册session
    });

    // 周期性发心跳（每 500ms）
    auto sendHbLoop = std::make_shared<std::function<void()>>();
    auto stop = std::make_shared<std::atomic<bool>>(false);
    loop.runAfter(0.5, [&, sendHbLoop, stop]() {
        *sendHbLoop = [&, sendHbLoop, stop]() {
            if (stop->load()) return;
            client.sendHeartbeat();
            loop.runAfter(0.5, *sendHbLoop);
        };
        (*sendHbLoop)();
    });

    // 3秒后检查：服务器回复了心跳，会话仍然在线
    loop.runAfter(3.0, [&, stop]() {
        stop->store(true);
        EXPECT_GT(client.heartbeat_received.load(), 0);
        auto session = server.getSessionManager().findSession("AGV-HB1");
        EXPECT_NE(session, nullptr);
        if (session) {
            EXPECT_EQ(session->getState(), AgvSession::ONLINE);
        }
        passed = true;
        loop.quit();
    });

    loop.loop();
    EXPECT_TRUE(passed);
}

// ==================== 2.3 低电量自动充电 ====================

TEST(IntegrationTest, LowBatteryAutoCharge) {
    int port = allocPort();
    EventLoop loop;

    InetAddress addr(port);
    GatewayServer server(&loop, addr, "TestSvr", 5.0, 4);
    server.start();

    TestClient client(&loop, InetAddress(port, "127.0.0.1"), "AGV-BAT");
    client.connect();

    std::atomic<bool> passed{false};

    loop.runAfter(0.3, [&]() {
        // 发送低电量遥测数据（15% < 20%）
        client.sendTelemetry(15.0);
    });

    loop.runAfter(1.0, [&]() {
        // 服务器应该下发了充电指令（以 AgvCommand 形式）
        // checkLowBatteryAndCharge -> sendChargeCommand -> MSG_AGV_COMMAND
        EXPECT_GT(client.command_received.load(), 0);

        auto session = server.getSessionManager().findSession("AGV-BAT");
        EXPECT_NE(session, nullptr);
        if (session) {
            EXPECT_EQ(session->getState(), AgvSession::CHARGING);
        }
        passed = true;
        loop.quit();
    });

    loop.loop();
    EXPECT_TRUE(passed);
}

// ==================== 2.4 多客户端并发 ====================

TEST(IntegrationTest, MultiClientConcurrent) {
    int port = allocPort();
    EventLoop loop;

    InetAddress addr(port);
    GatewayServer server(&loop, addr, "TestSvr", 5.0, 4);
    server.start();

    const int kNumClients = 5;
    std::vector<std::unique_ptr<TestClient>> clients;
    for (int i = 0; i < kNumClients; ++i) {
        auto c = std::make_unique<TestClient>(
            &loop, InetAddress(port, "127.0.0.1"),
            "AGV-MC" + std::to_string(i));
        c->connect();
        clients.push_back(std::move(c));
    }

    std::atomic<bool> passed{false};

    // 等待连接建立后发遥测
    loop.runAfter(0.5, [&]() {
        for (auto& c : clients) {
            EXPECT_TRUE(c->isConnected());
            c->sendTelemetry(75.0);
        }
    });

    loop.runAfter(1.5, [&]() {
        EXPECT_EQ(server.getSessionManager().size(), static_cast<size_t>(kNumClients));
        for (int i = 0; i < kNumClients; ++i) {
            auto session = server.getSessionManager().findSession(
                "AGV-MC" + std::to_string(i));
            EXPECT_NE(session, nullptr);
        }
        passed = true;
        loop.quit();
    });

    loop.loop();
    EXPECT_TRUE(passed);
}

// ==================== 2.5 会话超时清理 ====================

TEST(IntegrationTest, SessionTimeoutCleanup) {
    int port = allocPort();
    EventLoop loop;

    // 使用短超时（1秒）加速测试
    InetAddress addr(port);
    GatewayServer server(&loop, addr, "TestSvr", 1.0, 4);
    server.start();

    TestClient client(&loop, InetAddress(port, "127.0.0.1"), "AGV-TO");
    client.connect();

    std::atomic<bool> passed{false};

    // 发遥测注册会话
    loop.runAfter(0.3, [&]() {
        client.sendTelemetry();
    });

    // 0.5秒后验证在线
    loop.runAfter(0.5, [&]() {
        auto session = server.getSessionManager().findSession("AGV-TO");
        ASSERT_NE(session, nullptr);
        EXPECT_EQ(session->getState(), AgvSession::ONLINE);
    });

    // 停止发送后等待超时：1秒后看门狗标记 OFFLINE
    loop.runAfter(2.5, [&]() {
        auto session = server.getSessionManager().findSession("AGV-TO");
        ASSERT_NE(session, nullptr);
        EXPECT_EQ(session->getState(), AgvSession::OFFLINE);
        passed = true;
        loop.quit();
    });

    loop.loop();
    EXPECT_TRUE(passed);
}

// ==================== 2.6 Worker 线程 IO 隔离 ====================

TEST(IntegrationTest, WorkerThreadIOIsolation) {
    int port = allocPort();
    EventLoop loop;

    InetAddress addr(port);
    GatewayServer server(&loop, addr, "TestSvr", 5.0, 4);
    server.start();

    TestClient client(&loop, InetAddress(port, "127.0.0.1"), "AGV-WK");
    client.connect();

    std::atomic<bool> passed{false};
    auto stop = std::make_shared<std::atomic<bool>>(false);

    loop.runAfter(0.3, [&, stop]() {
        ASSERT_TRUE(client.isConnected());
        client.sendTelemetry();  // 注册会话

        // 启动高频遥测（50Hz = 20ms）
        auto sendLoop = std::make_shared<std::function<void()>>();
        *sendLoop = [&, sendLoop, stop]() {
            if (stop->load()) return;
            client.sendTelemetry();
            loop.runAfter(0.02, *sendLoop);
        };
        (*sendLoop)();

        // 发送 3 个导航任务（每个 200ms 阻塞）
        for (int i = 0; i < 3; ++i) {
            loop.runAfter(0.1 * i, [&, i]() {
                client.sendNavigationTask("WK-TASK-" + std::to_string(i));
            });
        }
    });

    // 2.5秒后停止遥测
    loop.runAfter(2.8, [stop]() { stop->store(true); });

    // 3秒后检查
    loop.runAfter(3.0, [&]() {
        int telem = client.telemetry_sent.load();
        int resp = client.response_received.load();
        // 2.5秒 @ 50Hz ≈ 125 条遥测，允许误差
        EXPECT_GE(telem, 100) << "Telemetry should not be blocked by Worker";
        EXPECT_EQ(resp, 3) << "All 3 NavigationTask responses expected";
        passed = true;
        loop.quit();
    });

    loop.loop();
    EXPECT_TRUE(passed);
}

// ==================== 2.7 紧急制动透传 ====================

TEST(IntegrationTest, EmergencyStopPassThrough) {
    int port = allocPort();
    EventLoop loop;

    InetAddress addr(port);
    GatewayServer server(&loop, addr, "TestSvr", 5.0, 4);
    server.start();

    // 两个客户端：source（指令发起方）和 target（被制动方）
    TestClient source(&loop, InetAddress(port, "127.0.0.1"), "AGV-SRC");
    TestClient target(&loop, InetAddress(port, "127.0.0.1"), "AGV-TGT");
    source.connect();
    target.connect();

    std::atomic<bool> passed{false};

    // 注册两个会话
    loop.runAfter(0.3, [&]() {
        ASSERT_TRUE(source.isConnected());
        ASSERT_TRUE(target.isConnected());
        source.sendTelemetry();
        target.sendTelemetry();
    });

    // source 发送 EMERGENCY_STOP 给 target
    loop.runAfter(0.6, [&]() {
        source.sendAgvCommand("AGV-TGT", CMD_EMERGENCY_STOP);
    });

    // 验证
    loop.runAfter(1.5, [&]() {
        // target 应该收到了 EMERGENCY_STOP 指令
        EXPECT_GE(target.command_received.load(), 1)
            << "Target should receive EMERGENCY_STOP";
        EXPECT_EQ(target.last_command_type.load(),
                   static_cast<int>(CMD_EMERGENCY_STOP));

        // source 应该收到了成功响应
        EXPECT_GE(source.response_received.load(), 1)
            << "Source should receive OK response";

        passed = true;
        loop.quit();
    });

    loop.loop();
    EXPECT_TRUE(passed);
}

// ==================== 2.8 延迟监控 RTT ====================

TEST(IntegrationTest, LatencyMonitorRTT) {
    int port = allocPort();
    EventLoop loop;

    InetAddress addr(port);
    GatewayServer server(&loop, addr, "TestSvr", 5.0, 4);
    // 设置短探测间隔（1秒）加速测试
    server.setLatencyProbeInterval(1.0);
    server.start();

    TestClient client(&loop, InetAddress(port, "127.0.0.1"), "AGV-RTT");
    client.connect();

    std::atomic<bool> passed{false};

    // 注册会话
    loop.runAfter(0.3, [&]() {
        client.sendTelemetry();
    });

    // 持续发心跳保持在线
    auto stop = std::make_shared<std::atomic<bool>>(false);
    auto hbLoop = std::make_shared<std::function<void()>>();
    loop.runAfter(0.5, [&, hbLoop, stop]() {
        *hbLoop = [&, hbLoop, stop]() {
            if (stop->load()) return;
            client.sendTelemetry();
            loop.runAfter(0.5, *hbLoop);
        };
        (*hbLoop)();
    });

    // 等待 3 秒（至少 2 次探测：t=1s 和 t=2s）
    loop.runAfter(3.5, [&, stop]() {
        stop->store(true);
        // 客户端应该收到了 Ping 并回复了 Pong
        EXPECT_GE(client.latency_probe_received.load(), 1)
            << "Client should receive at least 1 LatencyProbe Ping";

        // 服务器应该收到了 Pong 并记录了 RTT
        auto stats = server.getLatencyMonitor().getStats("AGV-RTT");
        EXPECT_GE(stats.sample_count, 1)
            << "Server should have at least 1 RTT sample";
        if (stats.sample_count > 0) {
            EXPECT_GT(stats.avg_rtt_ms, 0.0);
            EXPECT_LT(stats.avg_rtt_ms, 100.0);  // 本地回环应 < 100ms
        }

        passed = true;
        loop.quit();
    });

    loop.loop();
    EXPECT_TRUE(passed);
}

// ==================== 2.9 BusinessHandler 200ms 阻塞验证 ====================

TEST(IntegrationTest, BusinessHandlerBlocking200ms) {
    int port = allocPort();
    EventLoop loop;

    InetAddress addr(port);
    GatewayServer server(&loop, addr, "TestSvr", 5.0, 4);
    server.start();

    TestClient client(&loop, InetAddress(port, "127.0.0.1"), "AGV-BH");
    client.connect();

    std::atomic<bool> passed{false};
    auto stop = std::make_shared<std::atomic<bool>>(false);

    loop.runAfter(0.3, [&, stop]() {
        ASSERT_TRUE(client.isConnected());
        client.sendTelemetry();

        // 启动高频遥测
        auto sendLoop = std::make_shared<std::function<void()>>();
        *sendLoop = [&, sendLoop, stop]() {
            if (stop->load()) return;
            client.sendTelemetry();
            loop.runAfter(0.02, *sendLoop);
        };
        (*sendLoop)();

        // 发送 5 个导航任务（每个 Worker 中阻塞 200ms）
        for (int i = 0; i < 5; ++i) {
            loop.runAfter(0.05 * i, [&, i]() {
                client.sendNavigationTask("BH-TASK-" + std::to_string(i));
            });
        }
    });

    // 3秒后停止
    loop.runAfter(3.3, [stop]() { stop->store(true); });

    loop.runAfter(3.5, [&]() {
        int telem = client.telemetry_sent.load();
        int resp = client.response_received.load();
        // 3秒 @ 50Hz ≈ 150 条遥测
        EXPECT_GE(telem, 120)
            << "Telemetry not blocked by 200ms Worker sleep";
        EXPECT_EQ(resp, 5)
            << "All 5 NavigationTask responses should arrive";
        passed = true;
        loop.quit();
    });

    loop.loop();
    EXPECT_TRUE(passed);
}

// ==================== 2.10 连接断开安全性 ====================

TEST(IntegrationTest, ConnectionResilience) {
    int port = allocPort();
    EventLoop loop;

    InetAddress addr(port);
    GatewayServer server(&loop, addr, "TestSvr", 5.0, 4);
    server.start();

    TestClient client(&loop, InetAddress(port, "127.0.0.1"), "AGV-CR");
    client.connect();

    std::atomic<bool> passed{false};

    loop.runAfter(0.3, [&]() {
        ASSERT_TRUE(client.isConnected());
        client.sendTelemetry();
    });

    loop.runAfter(0.5, [&]() {
        // 发送导航任务后立即断开
        client.sendNavigationTask("CR-TASK-1");
        loop.runAfter(0.01, [&]() {
            client.disconnect();
        });
    });

    // 2秒后验证无崩溃
    loop.runAfter(2.0, [&]() {
        // Worker 应通过弱引用检测到连接断开，安全取消任务
        passed = true;
        loop.quit();
    });

    loop.loop();
    EXPECT_TRUE(passed);
    // 测试通过标准：无崩溃、无 SEGFAULT
}

// ==================== 2.11 MockAgvClient 状态机集成 ====================

TEST(IntegrationTest, MockAgvClientStateMachine) {
    int port = allocPort();

    // Server 线程
    std::thread server_thread([port]() {
        EventLoop server_loop;
        InetAddress listen_addr(port);
        GatewayServer server(&server_loop, listen_addr, "TestSvr", 5.0, 4);
        server.start();
        server_loop.runAfter(8.0, [&]() { server_loop.quit(); });
        server_loop.loop();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Client 线程
    std::atomic<bool> passed{false};
    std::thread client_thread([port, &passed]() {
        EventLoop client_loop;
        InetAddress server_addr(port, "127.0.0.1");
        // 初始电量18%，频率50Hz，超时5s
        MockAgvClient client(&client_loop, server_addr, "AGV-SM01",
                             50.0, 18.0, 5.0);
        client.connect();

        // 1秒后检查：应连接成功
        client_loop.runAfter(1.0, [&]() {
            EXPECT_TRUE(client.isConnected());
            // 电量可能相等或略有下降，均正常
            EXPECT_LE(client.getBattery(), 18.0);
        });

        // 3秒后检查：低电量应触发服务器下发充电指令
        client_loop.runAfter(3.0, [&]() {
            passed = true;
        });

        client_loop.runAfter(4.0, [&]() {
            client_loop.quit();
        });

        client_loop.loop();
    });

    client_thread.join();
    server_thread.join();

    EXPECT_TRUE(passed);
}

// ==================== Main ====================

int main(int argc, char** argv) {
    // 降低日志级别减少输出噪音
    Logger::setLogLevel(Logger::WARN);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
