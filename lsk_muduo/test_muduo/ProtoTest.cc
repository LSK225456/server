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
    
    EXPECT_EQ(TASK_IDLE, 0);
    EXPECT_EQ(TASK_RUNNING, 1);
    EXPECT_EQ(TASK_COMPLETED, 2);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
