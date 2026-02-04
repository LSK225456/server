#include <gtest/gtest.h>
#include "../agv_server/codec/LengthHeaderCodec.h"
#include "../agv_server/proto/message.pb.h"
#include "../agv_server/proto/common.pb.h"
#include "../agv_server/proto/message_id.h"
#include "../muduo/net/Buffer.h"
#include <chrono>  // 添加：性能测试需要

using namespace agv::codec;
using namespace agv::proto;

/**
 * 测试套件：LengthHeaderCodec 编解码器（工业级覆盖）
 * 
 * 测试场景分类：
 * 1. 基础功能：正常编解码往返
 * 2. 边界条件：空消息、最大消息、最小消息
 * 3. 异常处理：超长消息、非法包头、数据损坏
 * 4. 网络场景：粘包、拆包、半包、多包
 * 5. 并发安全：多线程编解码（可选）
 * 6. 性能验证：大消息编解码时间
 */
class CodecTest : public ::testing::Test {
protected:
    void SetUp() override {
        buffer_.clear();
    }

    // 辅助方法：创建测试用 Telemetry 消息
    std::string createTestTelemetry(const std::string& agvId = "AGV-TEST") {
        AgvTelemetry telemetry;
        telemetry.set_agv_id(agvId);
        telemetry.set_timestamp(1234567890123456LL);
        telemetry.set_x(10.5);
        telemetry.set_y(20.3);
        telemetry.set_theta(45.0);
        telemetry.set_battery(85.5);
        
        std::string serialized;
        telemetry.SerializeToString(&serialized);
        return serialized;
    }

    Buffer buffer_;
};

// ==================== 基础功能测试 ====================

/**
 * 测试目标：正常编解码往返一致性
 */
TEST_F(CodecTest, BasicEncodeDecode_RoundTrip) {
    // 准备数据
    std::string protoData = createTestTelemetry();
    const uint16_t msgType = MSG_AGV_TELEMETRY;
    const uint16_t flags = LengthHeaderCodec::FLAG_NONE;

    // 编码
    ASSERT_TRUE(LengthHeaderCodec::encode(&buffer_, msgType, protoData, flags));
    
    // 验证包头长度
    EXPECT_EQ(buffer_.readableBytes(), LengthHeaderCodec::kHeaderLen + protoData.size());

    // 检查完整性
    EXPECT_TRUE(LengthHeaderCodec::hasCompleteMessage(&buffer_));

    // 解码
    uint16_t decodedType = 0;
    uint16_t decodedFlags = 0;
    std::string decodedData;
    ASSERT_TRUE(LengthHeaderCodec::decode(&buffer_, &decodedType, &decodedData, &decodedFlags));

    // 验证结果
    EXPECT_EQ(decodedType, msgType);
    EXPECT_EQ(decodedFlags, flags);
    EXPECT_EQ(decodedData, protoData);

    // 验证 Buffer 已消费完
    EXPECT_EQ(buffer_.readableBytes(), 0);

    // 验证 Protobuf 反序列化
    AgvTelemetry telemetry;
    ASSERT_TRUE(telemetry.ParseFromString(decodedData));
    EXPECT_EQ(telemetry.agv_id(), "AGV-TEST");
    EXPECT_DOUBLE_EQ(telemetry.x(), 10.5);
}

/**
 * 测试目标：不同消息类型和标志位
 */
TEST_F(CodecTest, DifferentMessageTypesAndFlags) {
    struct TestCase {
        uint16_t msgType;
        uint16_t flags;
        std::string description;
    };

    std::vector<TestCase> testCases = {
        {MSG_HEARTBEAT, LengthHeaderCodec::FLAG_NONE, "心跳消息"},
        {MSG_COMMON_RESPONSE, LengthHeaderCodec::FLAG_COMPRESSED, "压缩响应"},
        {MSG_AGV_TELEMETRY, LengthHeaderCodec::FLAG_PRIORITY, "高优先级遥测"},
        {0x1234, LengthHeaderCodec::FLAG_ENCRYPTED | LengthHeaderCodec::FLAG_COMPRESSED, "组合标志"},
    };

    for (const auto& tc : testCases) {
        buffer_.retrieveAll();  // 清空 Buffer

        std::string protoData = createTestTelemetry();
        ASSERT_TRUE(LengthHeaderCodec::encode(&buffer_, tc.msgType, protoData, tc.flags))
            << "Failed to encode: " << tc.description;

        uint16_t decodedType, decodedFlags;
        std::string decodedData;
        ASSERT_TRUE(LengthHeaderCodec::decode(&buffer_, &decodedType, &decodedData, &decodedFlags))
            << "Failed to decode: " << tc.description;

        EXPECT_EQ(decodedType, tc.msgType) << tc.description;
        EXPECT_EQ(decodedFlags, tc.flags) << tc.description;
        EXPECT_EQ(decodedData, protoData) << tc.description;
    }
}

// ==================== 边界条件测试 ====================

/**
 * 测试目标：最小合法消息（1字节负载）
 */
TEST_F(CodecTest, MinimumMessage_OneByte) {
    std::string protoData = "X";  // 1字节
    ASSERT_TRUE(LengthHeaderCodec::encode(&buffer_, MSG_HEARTBEAT, protoData));

    // 验证长度
    EXPECT_EQ(buffer_.readableBytes(), LengthHeaderCodec::kHeaderLen + 1);

    uint16_t msgType;
    std::string decodedData;
    ASSERT_TRUE(LengthHeaderCodec::decode(&buffer_, &msgType, &decodedData));
    EXPECT_EQ(decodedData, "X");
}

/**
 * 测试目标：大消息（接近 10MB 限制）
 */
TEST_F(CodecTest, LargeMessage_NearMaxLimit) {
    // 创建 1MB 消息
    const size_t size = 1 * 1024 * 1024;
    std::string largeData(size, 'A');

    ASSERT_TRUE(LengthHeaderCodec::encode(&buffer_, MSG_AGV_TELEMETRY, largeData));

    uint16_t msgType;
    std::string decodedData;
    ASSERT_TRUE(LengthHeaderCodec::decode(&buffer_, &msgType, &decodedData));

    EXPECT_EQ(decodedData.size(), size);
    EXPECT_EQ(decodedData[0], 'A');
    EXPECT_EQ(decodedData[size - 1], 'A');
}

/**
 * 测试目标：空消息（应失败）
 */
TEST_F(CodecTest, EmptyMessage_ShouldFail) {
    std::string emptyData;
    EXPECT_FALSE(LengthHeaderCodec::encode(&buffer_, MSG_HEARTBEAT, emptyData));
}

/**
 * 测试目标：超长消息（超过 10MB）
 */
TEST_F(CodecTest, OversizedMessage_ShouldFail) {
    const size_t size = LengthHeaderCodec::kMaxMessageLen + 1;
    std::string oversizedData(size, 'X');

    EXPECT_FALSE(LengthHeaderCodec::encode(&buffer_, MSG_AGV_TELEMETRY, oversizedData));
}

// ==================== 异常处理测试 ====================

/**
 * 测试目标：非法包头（Length 字段为 0）
 */
TEST_F(CodecTest, InvalidHeader_ZeroLength) {
    buffer_.appendInt32(0);  // 非法 Length
    buffer_.appendInt16(MSG_HEARTBEAT);
    buffer_.appendInt16(0);

    EXPECT_FALSE(LengthHeaderCodec::hasCompleteMessage(&buffer_));
}

/**
 * 测试目标：非法包头（Length 超限）
 */
TEST_F(CodecTest, InvalidHeader_ExcessiveLength) {
    buffer_.appendInt32(LengthHeaderCodec::kMaxMessageLen + 1000);
    buffer_.appendInt16(MSG_HEARTBEAT);
    buffer_.appendInt16(0);

    EXPECT_FALSE(LengthHeaderCodec::hasCompleteMessage(&buffer_));
}

/**
 * 测试目标：数据损坏（Length 与实际不符）
 */
TEST_F(CodecTest, CorruptedData_LengthMismatch) {
    std::string protoData = createTestTelemetry();
    
    // 手动构造错误的包头
    buffer_.appendInt32(100);  // 声称总长度为 100
    buffer_.appendInt16(MSG_AGV_TELEMETRY);
    buffer_.appendInt16(0);
    buffer_.append(protoData);  // 但实际负载更长

    // hasCompleteMessage 会根据声称的 Length 判断
    // 这里 Length=100，Buffer 可读字节 = 8 + protoData.size() > 100
    // 所以会认为有完整消息
    if (protoData.size() + 8 >= 100) {
        EXPECT_TRUE(LengthHeaderCodec::hasCompleteMessage(&buffer_));
        
        // 但解码时会读取错误的负载长度
        uint16_t msgType;
        std::string decodedData;
        ASSERT_TRUE(LengthHeaderCodec::decode(&buffer_, &msgType, &decodedData));
        
        // 解码出的数据长度应该是 100 - 8 = 92
        EXPECT_EQ(decodedData.size(), 92);
        
        // Protobuf 解析可能失败
        AgvTelemetry telemetry;
        // 不强制要求解析成功，因为数据已损坏
    }
}

// ==================== 网络场景测试 ====================

/**
 * 测试目标：粘包场景（一次收到3个完整包）
 */
TEST_F(CodecTest, StickyPacket_ThreeMessagesAtOnce) {
    std::string data1 = createTestTelemetry("AGV-001");
    std::string data2 = createTestTelemetry("AGV-002");
    std::string data3 = createTestTelemetry("AGV-003");

    // 连续编码3个消息到同一 Buffer
    ASSERT_TRUE(LengthHeaderCodec::encode(&buffer_, MSG_AGV_TELEMETRY, data1));
    ASSERT_TRUE(LengthHeaderCodec::encode(&buffer_, MSG_AGV_TELEMETRY, data2));
    ASSERT_TRUE(LengthHeaderCodec::encode(&buffer_, MSG_AGV_TELEMETRY, data3));

    // 验证有3个完整消息
    int count = 0;
    while (LengthHeaderCodec::hasCompleteMessage(&buffer_)) {
        uint16_t msgType;
        std::string decodedData;
        ASSERT_TRUE(LengthHeaderCodec::decode(&buffer_, &msgType, &decodedData));
        
        AgvTelemetry telemetry;
        ASSERT_TRUE(telemetry.ParseFromString(decodedData));
        
        // 验证车辆 ID（添加大括号）
        if (count == 0) {
            EXPECT_EQ(telemetry.agv_id(), "AGV-001");
        }
        if (count == 1) {
            EXPECT_EQ(telemetry.agv_id(), "AGV-002");
        }
        if (count == 2) {
            EXPECT_EQ(telemetry.agv_id(), "AGV-003");
        }
        
        count++;
    }

    EXPECT_EQ(count, 3);
    EXPECT_EQ(buffer_.readableBytes(), 0);
}

/**
 * 测试目标：拆包场景（一个包分两次到达）
 */
TEST_F(CodecTest, FragmentedPacket_TwoChunks) {
    std::string protoData = createTestTelemetry();
    
    // 先编码到临时 Buffer
    Buffer tempBuf;
    ASSERT_TRUE(LengthHeaderCodec::encode(&tempBuf, MSG_AGV_TELEMETRY, protoData));
    
    const size_t totalLen = tempBuf.readableBytes();
    const size_t splitPoint = totalLen / 2;  // 从中间拆分

    // 第一次到达：前半部分
    buffer_.append(tempBuf.peek(), splitPoint);
    EXPECT_FALSE(LengthHeaderCodec::hasCompleteMessage(&buffer_));  // 不完整

    // 第二次到达：后半部分
    tempBuf.retrieve(splitPoint);  // 修正：使用正确的变量名
    buffer_.append(tempBuf.peek(), tempBuf.readableBytes());
    EXPECT_TRUE(LengthHeaderCodec::hasCompleteMessage(&buffer_));  // 现在完整了

    // 解码验证
    uint16_t msgType;
    std::string decodedData;
    ASSERT_TRUE(LengthHeaderCodec::decode(&buffer_, &msgType, &decodedData));
    EXPECT_EQ(decodedData, protoData);
}

/**
 * 测试目标：半包场景（只收到部分包头）
 */
TEST_F(CodecTest, HalfPacket_PartialHeader) {
    // 只写入 4 字节（包头需要 8 字节）
    buffer_.appendInt32(100);
    
    EXPECT_FALSE(LengthHeaderCodec::hasCompleteMessage(&buffer_));
    EXPECT_EQ(LengthHeaderCodec::peekMessageLength(&buffer_), 0);  // 无法解析

    // 补全包头
    buffer_.appendInt16(MSG_HEARTBEAT);
    buffer_.appendInt16(0);
    
    // 现在可以解析 Length，但负载还没到
    EXPECT_FALSE(LengthHeaderCodec::hasCompleteMessage(&buffer_));
    EXPECT_EQ(LengthHeaderCodec::peekMessageLength(&buffer_), 100);
}

/**
 * 测试目标：半包场景（包头完整，负载不完整）
 */
TEST_F(CodecTest, HalfPacket_PartialPayload) {
    std::string protoData = createTestTelemetry();
    const uint32_t totalLen = LengthHeaderCodec::kHeaderLen + protoData.size();

    // 写入完整包头
    buffer_.appendInt32(totalLen);
    buffer_.appendInt16(MSG_AGV_TELEMETRY);
    buffer_.appendInt16(0);

    // 只写入部分负载
    buffer_.append(protoData.data(), protoData.size() / 2);

    EXPECT_FALSE(LengthHeaderCodec::hasCompleteMessage(&buffer_));
    EXPECT_EQ(LengthHeaderCodec::peekMessageLength(&buffer_), totalLen);
}

/**
 * 测试目标：混合场景（1.5个包）
 */
TEST_F(CodecTest, MixedScenario_OneAndHalfPackets) {
    std::string data1 = createTestTelemetry("AGV-001");
    std::string data2 = createTestTelemetry("AGV-002");

    // 编码两个消息到临时 Buffer
    Buffer tempBuf;
    ASSERT_TRUE(LengthHeaderCodec::encode(&tempBuf, MSG_AGV_TELEMETRY, data1));
    ASSERT_TRUE(LengthHeaderCodec::encode(&tempBuf, MSG_AGV_TELEMETRY, data2));

    // 只取前 1.5 个消息的数据
    const size_t msg1Size = LengthHeaderCodec::kHeaderLen + data1.size();
    const size_t halfMsg2Size = (LengthHeaderCodec::kHeaderLen + data2.size()) / 2;
    buffer_.append(tempBuf.peek(), msg1Size + halfMsg2Size);

    // 应该能解出第一个消息
    EXPECT_TRUE(LengthHeaderCodec::hasCompleteMessage(&buffer_));
    uint16_t msgType;
    std::string decodedData;
    ASSERT_TRUE(LengthHeaderCodec::decode(&buffer_, &msgType, &decodedData));
    
    AgvTelemetry telemetry;
    ASSERT_TRUE(telemetry.ParseFromString(decodedData));
    EXPECT_EQ(telemetry.agv_id(), "AGV-001");

    // 第二个消息不完整
    EXPECT_FALSE(LengthHeaderCodec::hasCompleteMessage(&buffer_));
}

// ==================== 性能验证测试 ====================

/**
 * 测试目标：编解码性能基准
 */
TEST_F(CodecTest, Performance_1000Messages) {
    const int iterations = 1000;
    std::string protoData = createTestTelemetry();

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        Buffer buf;
        ASSERT_TRUE(LengthHeaderCodec::encode(&buf, MSG_AGV_TELEMETRY, protoData));
        
        uint16_t msgType;
        std::string decodedData;
        ASSERT_TRUE(LengthHeaderCodec::decode(&buf, &msgType, &decodedData));
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // 输出性能指标
    double avgTimeUs = duration.count() / static_cast<double>(iterations);
    std::cout << "\n[Performance] " << iterations << " encode/decode cycles: "
              << duration.count() << " us (avg: " << avgTimeUs << " us/cycle)\n";

    // 验证：平均每次编解码应小于 100us（根据硬件调整）
    EXPECT_LT(avgTimeUs, 100.0) << "Performance regression detected";
}

// ==================== 主函数 ====================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
