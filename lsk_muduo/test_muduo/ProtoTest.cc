#include <gtest/gtest.h>
#include "../agv_server/proto/message.pb.h"
#include "../agv_server/proto/common.pb.h"
#include "../agv_server/proto/message_id.h"
#include <cmath>

using namespace agv::proto;

/**
 * 测试套件：Protobuf 消息序列化与反序列化（迭代一范围）
 * 覆盖场景：
 * 1. AgvTelemetry（上行遥测）序列化往返一致性
 * 2. CommonResponse（下行响应）正确性
 * 3. Heartbeat（心跳）基本功能
 * 4. 边界值处理
 * 5. 默认值验证
 */
class ProtobufMessageTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 每个测试前执行
    }
};

// ==================== AgvTelemetry 测试 ====================

/**
 * 测试目标：AgvTelemetry 序列化往返一致性
 */
TEST_F(ProtobufMessageTest, AgvTelemetry_Serialization_RoundTrip) {
    // 构造原始消息
    AgvTelemetry original;
    original.set_agv_id("AGV-001");
    original.set_timestamp(1234567890123456LL);  // 微秒时间戳
    original.set_x(10.5);
    original.set_y(-5.3);
    original.set_theta(45.0);  // 度
    original.set_confidence(0.95);
    original.set_linear_velocity(1.67);  // 米/秒
    original.set_angular_velocity(15.0);  // 度/秒
    original.set_acceleration(0.5);
    original.set_payload_weight(500.0);  // 500 kg
    original.set_battery(85.5);
    original.set_error_code(0);
    original.set_fork_height(0.3);
    
    // 序列化
    std::string serialized;
    ASSERT_TRUE(original.SerializeToString(&serialized));
    EXPECT_GT(serialized.size(), 0) << "序列化后应有数据";
    
    // 反序列化
    AgvTelemetry deserialized;
    ASSERT_TRUE(deserialized.ParseFromString(serialized));
    
    // 验证所有字段
    EXPECT_EQ(deserialized.agv_id(), "AGV-001");
    EXPECT_EQ(deserialized.timestamp(), 1234567890123456LL);
    EXPECT_DOUBLE_EQ(deserialized.x(), 10.5);
    EXPECT_DOUBLE_EQ(deserialized.y(), -5.3);
    EXPECT_DOUBLE_EQ(deserialized.theta(), 45.0);
    EXPECT_DOUBLE_EQ(deserialized.confidence(), 0.95);
    EXPECT_DOUBLE_EQ(deserialized.linear_velocity(), 1.67);
    EXPECT_DOUBLE_EQ(deserialized.angular_velocity(), 15.0);
    EXPECT_DOUBLE_EQ(deserialized.acceleration(), 0.5);
    EXPECT_DOUBLE_EQ(deserialized.payload_weight(), 500.0);
    EXPECT_DOUBLE_EQ(deserialized.battery(), 85.5);
    EXPECT_EQ(deserialized.error_code(), 0);
    EXPECT_DOUBLE_EQ(deserialized.fork_height(), 0.3);
}

/**
 * 测试目标：极端值的正确处理
 */
TEST_F(ProtobufMessageTest, AgvTelemetry_BoundaryValues) {
    AgvTelemetry msg;
    
    // 测试极端坐标
    msg.set_x(1e6);   // 1000 km（极端远距离）
    msg.set_y(-1e6);
    msg.set_theta(720.0);  // 两圈
    msg.set_confidence(0.0);  // 完全不可信
    msg.set_linear_velocity(10.0);  // 超高速
    msg.set_angular_velocity(-360.0);  // 快速旋转
    msg.set_acceleration(-5.0);  // 紧急制动
    msg.set_payload_weight(0.0);  // 空载
    msg.set_battery(0.0);  // 电量耗尽
    msg.set_error_code(UINT32_MAX);  // 最大故障码
    
    // 序列化往返
    std::string serialized;
    ASSERT_TRUE(msg.SerializeToString(&serialized));
    
    AgvTelemetry deserialized;
    ASSERT_TRUE(deserialized.ParseFromString(serialized));
    
    // 验证
    EXPECT_DOUBLE_EQ(deserialized.x(), 1e6);
    EXPECT_DOUBLE_EQ(deserialized.y(), -1e6);
    EXPECT_DOUBLE_EQ(deserialized.theta(), 720.0);
    EXPECT_DOUBLE_EQ(deserialized.confidence(), 0.0);
    EXPECT_EQ(deserialized.error_code(), UINT32_MAX);
}

/**
 * 测试目标：字段缺失时的 proto3 默认值
 */
TEST_F(ProtobufMessageTest, AgvTelemetry_DefaultValues) {
    AgvTelemetry msg;
    
    // proto3 的默认值：数值为 0，string 为空
    EXPECT_EQ(msg.agv_id(), "");
    EXPECT_EQ(msg.timestamp(), 0);
    EXPECT_DOUBLE_EQ(msg.x(), 0.0);
    EXPECT_DOUBLE_EQ(msg.payload_weight(), 0.0);
    EXPECT_EQ(msg.error_code(), 0);
}

// ==================== CommonResponse 测试 ====================

/**
 * 测试目标：CommonResponse 的正确性
 */
TEST_F(ProtobufMessageTest, CommonResponse_Serialization) {
    CommonResponse response;
    response.set_status(STATUS_OK);
    response.set_message("操作成功");
    response.set_timestamp(1234567890123456LL);
    
    // 序列化
    std::string serialized;
    ASSERT_TRUE(response.SerializeToString(&serialized));
    
    // 反序列化
    CommonResponse deserialized;
    ASSERT_TRUE(deserialized.ParseFromString(serialized));
    
    EXPECT_EQ(deserialized.status(), STATUS_OK);
    EXPECT_EQ(deserialized.message(), "操作成功");
    EXPECT_EQ(deserialized.timestamp(), 1234567890123456LL);
}

/**
 * 测试目标：错误状态码
 */
TEST_F(ProtobufMessageTest, CommonResponse_ErrorCodes) {
    CommonResponse response;
    response.set_status(STATUS_INVALID_REQUEST);
    response.set_message("无效的请求");
    
    std::string serialized;
    ASSERT_TRUE(response.SerializeToString(&serialized));
    
    CommonResponse deserialized;
    ASSERT_TRUE(deserialized.ParseFromString(serialized));
    
    EXPECT_EQ(deserialized.status(), STATUS_INVALID_REQUEST);
    EXPECT_EQ(deserialized.message(), "无效的请求");
}

// ==================== Heartbeat 测试 ====================

/**
 * 测试目标：Heartbeat 消息
 */
TEST_F(ProtobufMessageTest, Heartbeat_Serialization) {
    Heartbeat heartbeat;
    heartbeat.set_agv_id("AGV-001");
    heartbeat.set_timestamp(1234567890123456LL);
    
    // 序列化
    std::string serialized;
    ASSERT_TRUE(heartbeat.SerializeToString(&serialized));
    
    // 反序列化
    Heartbeat deserialized;
    ASSERT_TRUE(deserialized.ParseFromString(serialized));
    
    EXPECT_EQ(deserialized.agv_id(), "AGV-001");
    EXPECT_EQ(deserialized.timestamp(), 1234567890123456LL);
}

// ==================== 枚举类型测试 ====================

/**
 * 测试目标：枚举值的正确性
 */
TEST_F(ProtobufMessageTest, EnumValues) {
    // StatusCode - 根据实际 common.proto 定义
    EXPECT_EQ(STATUS_OK, 0);
    EXPECT_EQ(STATUS_INVALID_REQUEST, 1);
    EXPECT_EQ(STATUS_INTERNAL_ERROR, 2);  // 修正顺序
    EXPECT_EQ(STATUS_TIMEOUT, 3);          // 修正顺序
}

// ==================== 消息 ID 测试 ====================

/**
 * 测试目标：消息 ID 分配和辅助函数（迭代一范围）
 */
TEST_F(ProtobufMessageTest, MessageIdConstants) {
    // 上行消息
    EXPECT_TRUE(isUpstreamMessage(MSG_AGV_TELEMETRY));
    EXPECT_FALSE(isUpstreamMessage(MSG_HEARTBEAT));
    
    // 下行消息 - MSG_COMMON_RESPONSE 是通用消息，不是下行消息
    EXPECT_FALSE(isDownstreamMessage(MSG_COMMON_RESPONSE));
    EXPECT_FALSE(isDownstreamMessage(MSG_AGV_TELEMETRY));
    
    // 通用消息
    EXPECT_TRUE(isCommonMessage(MSG_HEARTBEAT));
    EXPECT_TRUE(isCommonMessage(MSG_COMMON_RESPONSE));
    
    // 消息名称
    EXPECT_STREQ(getMessageTypeName(MSG_AGV_TELEMETRY), "AgvTelemetry");
    EXPECT_STREQ(getMessageTypeName(MSG_HEARTBEAT), "Heartbeat");
    EXPECT_STREQ(getMessageTypeName(MSG_COMMON_RESPONSE), "CommonResponse");
    EXPECT_STREQ(getMessageTypeName(0xFFFF), "Unknown");
}

// ==================== 主函数 ====================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
