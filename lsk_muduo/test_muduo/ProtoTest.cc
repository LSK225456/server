#include <gtest/gtest.h>
#include "message.pb.h"
#include "common.pb.h"

using namespace agv::proto;

// 测试 AgvTelemetry 序列化与反序列化
TEST(ProtoTest, AgvTelemetrySerialization) {
    AgvTelemetry original;
    original.set_agv_id("AGV001");
    original.set_timestamp(1234567890);
    original.set_x(1.5);
    original.set_y(2.5);
    original.set_theta(0.5);
    original.set_confidence(0.95);
    original.set_battery(80.0);
    
    // 序列化
    std::string data;
    EXPECT_TRUE(original.SerializeToString(&data));
    EXPECT_FALSE(data.empty());
    
    // 反序列化
    AgvTelemetry parsed;
    EXPECT_TRUE(parsed.ParseFromString(data));
    
    EXPECT_EQ(parsed.agv_id(), "AGV001");
    EXPECT_EQ(parsed.timestamp(), 1234567890);
    EXPECT_DOUBLE_EQ(parsed.x(), 1.5);
    EXPECT_DOUBLE_EQ(parsed.y(), 2.5);
    EXPECT_DOUBLE_EQ(parsed.theta(), 0.5);
    EXPECT_DOUBLE_EQ(parsed.confidence(), 0.95);
    EXPECT_DOUBLE_EQ(parsed.battery(), 80.0);
}

// 测试 Heartbeat 消息
TEST(ProtoTest, HeartbeatSerialization) {
    Heartbeat original;
    original.set_agv_id("AGV002");
    original.set_timestamp(9876543210);
    
    std::string data;
    EXPECT_TRUE(original.SerializeToString(&data));
    
    Heartbeat parsed;
    EXPECT_TRUE(parsed.ParseFromString(data));
    
    EXPECT_EQ(parsed.agv_id(), "AGV002");
    EXPECT_EQ(parsed.timestamp(), 9876543210);
}

// 测试 AgvCommand 消息
TEST(ProtoTest, AgvCommandSerialization) {
    AgvCommand original;
    original.set_target_agv_id("AGV003");
    original.set_timestamp(1111111111);
    original.set_cmd_type(CMD_EMERGENCY_STOP);
    
    std::string data;
    EXPECT_TRUE(original.SerializeToString(&data));
    
    AgvCommand parsed;
    EXPECT_TRUE(parsed.ParseFromString(data));
    
    EXPECT_EQ(parsed.target_agv_id(), "AGV003");
    EXPECT_EQ(parsed.cmd_type(), CMD_EMERGENCY_STOP);
}

// 测试枚举值
TEST(ProtoTest, EnumValues) {
    EXPECT_EQ(CMD_EMERGENCY_STOP, 0);
    EXPECT_EQ(CMD_RESUME, 1);
    EXPECT_EQ(CMD_PAUSE, 2);
    EXPECT_EQ(CMD_REBOOT, 3);
    EXPECT_EQ(CMD_NAVIGATE_TO, 4);  // 迭代三新增
    
    EXPECT_EQ(TASK_IDLE, 0);
    EXPECT_EQ(TASK_RUNNING, 1);
    EXPECT_EQ(TASK_COMPLETED, 2);
    
    EXPECT_EQ(OP_MOVE_ONLY, 0);
    EXPECT_EQ(OP_PICK_UP, 1);
    EXPECT_EQ(OP_PUT_DOWN, 2);
}

// 测试 NavigationTask 消息（迭代三新增）
TEST(ProtoTest, NavigationTaskSerialization) {
    NavigationTask original;
    original.set_target_agv_id("AGV004");
    original.set_timestamp(2222222222);
    original.set_task_id("TASK-001");
    
    // 设置目标点
    Point* target = original.mutable_target_node();
    target->set_x(100.0);
    target->set_y(200.0);
    
    original.set_operation(OP_PICK_UP);
    
    // 添加路径点
    for (int i = 0; i < 3; ++i) {
        Point* pt = original.add_global_path();
        pt->set_x(10.0 * i);
        pt->set_y(20.0 * i);
    }
    
    // 序列化
    std::string data;
    EXPECT_TRUE(original.SerializeToString(&data));
    
    // 反序列化
    NavigationTask parsed;
    EXPECT_TRUE(parsed.ParseFromString(data));
    
    EXPECT_EQ(parsed.target_agv_id(), "AGV004");
    EXPECT_EQ(parsed.task_id(), "TASK-001");
    EXPECT_DOUBLE_EQ(parsed.target_node().x(), 100.0);
    EXPECT_DOUBLE_EQ(parsed.target_node().y(), 200.0);
    EXPECT_EQ(parsed.operation(), OP_PICK_UP);
    EXPECT_EQ(parsed.global_path_size(), 3);
    EXPECT_DOUBLE_EQ(parsed.global_path(1).x(), 10.0);
    EXPECT_DOUBLE_EQ(parsed.global_path(1).y(), 20.0);
}

// 测试 LatencyProbe 消息（迭代三新增）
TEST(ProtoTest, LatencyProbeSerialization) {
    LatencyProbe original;
    original.set_target_agv_id("AGV005");
    original.set_send_timestamp(3333333333);
    original.set_seq_num(12345);
    original.set_is_response(false);
    
    // 序列化
    std::string data;
    EXPECT_TRUE(original.SerializeToString(&data));
    
    // 反序列化
    LatencyProbe parsed;
    EXPECT_TRUE(parsed.ParseFromString(data));
    
    EXPECT_EQ(parsed.target_agv_id(), "AGV005");
    EXPECT_EQ(parsed.send_timestamp(), 3333333333);
    EXPECT_EQ(parsed.seq_num(), 12345);
    EXPECT_FALSE(parsed.is_response());
}

// 测试 CommonResponse 消息
TEST(ProtoTest, CommonResponseSerialization) {
    CommonResponse original;
    original.set_status(STATUS_OK);
    original.set_message("Success");
    original.set_timestamp(4444444444);
    
    // 序列化
    std::string data;
    EXPECT_TRUE(original.SerializeToString(&data));
    
    // 反序列化
    CommonResponse parsed;
    EXPECT_TRUE(parsed.ParseFromString(data));
    
    EXPECT_EQ(parsed.status(), STATUS_OK);
    EXPECT_EQ(parsed.message(), "Success");
    EXPECT_EQ(parsed.timestamp(), 4444444444);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
