#include "../muduo/net/Buffer.h"
#include <gtest/gtest.h>
#include <cstring>
#include <vector>
#include <climits>

/**
 * 测试套件：Buffer 整数操作
 * 覆盖场景：
 * 1. 字节序转换正确性（0x12345678 -> 12 34 56 78）
 * 2. appendInt32 和 readInt32 的往返一致性
 * 3. peekInt32 不移动读指针
 * 4. prependInt32 在头部插入
 * 5. 边界值测试（0, -1, INT32_MAX, INT32_MIN）
 */
class BufferIntegerTest : public ::testing::Test {
protected:
    Buffer buffer;
};

// ==================== 核心测试：字节序验证 ====================

/**
 * 测试目标：验证写入 0x12345678 后，内存中确实是 12 34 56 78（大端序）
 */
TEST_F(BufferIntegerTest, AppendInt32_ByteOrder_BigEndian) {
    // 写入测试值 0x12345678
    int32_t testValue = 0x12345678;
    buffer.appendInt32(testValue);
    
    // 获取 Buffer 内部存储的原始字节
    const char* data = buffer.peek();
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data);
    
    // 验证字节序：大端序应该是 12 34 56 78
    EXPECT_EQ(bytes[0], 0x12) << "第 1 字节应为 0x12";
    EXPECT_EQ(bytes[1], 0x34) << "第 2 字节应为 0x34";
    EXPECT_EQ(bytes[2], 0x56) << "第 3 字节应为 0x56";
    EXPECT_EQ(bytes[3], 0x78) << "第 4 字节应为 0x78";
    
    // 验证可读字节数
    EXPECT_EQ(buffer.readableBytes(), sizeof(int32_t));
}

// ==================== 读写一致性测试 ====================

/**
 * 测试目标：写入的整数能正确读取出来（往返一致性）
 */
TEST_F(BufferIntegerTest, AppendAndRead_RoundTrip) {
    int32_t original = 0xABCDEF01;
    
    buffer.appendInt32(original);
    int32_t result = buffer.readInt32();
    
    EXPECT_EQ(result, original) << "读取值应与写入值相同";
    EXPECT_EQ(buffer.readableBytes(), 0) << "读取后应无剩余可读字节";
}

/**
 * 测试目标：连续写入多个整数，能按顺序正确读取
 */
TEST_F(BufferIntegerTest, MultipleIntegers_SequentialReadWrite) {
    // 修复 narrowing conversion：使用显式转换或直接写 INT32_MIN
    std::vector<int32_t> testData = {100, -200, 0x7FFFFFFF, INT32_MIN, 0};
    
    // 写入
    for (int32_t val : testData) {
        buffer.appendInt32(val);
    }
    
    // 读取并验证
    for (int32_t expected : testData) {
        ASSERT_GE(buffer.readableBytes(), sizeof(int32_t)) << "Buffer 应有足够数据";
        int32_t actual = buffer.readInt32();
        EXPECT_EQ(actual, expected) << "读取值应与写入顺序一致";
    }
    
    EXPECT_EQ(buffer.readableBytes(), 0) << "所有数据应被读取完";
}

// ==================== peekInt32 测试 ====================

/**
 * 测试目标：peekInt32 应该不移动读指针
 */
TEST_F(BufferIntegerTest, PeekInt32_DoesNotMoveReadIndex) {
    int32_t value = 0x12345678;
    buffer.appendInt32(value);
    
    size_t readableBefore = buffer.readableBytes();
    
    // 连续 peek 三次
    int32_t peek1 = buffer.peekInt32();
    int32_t peek2 = buffer.peekInt32();
    int32_t peek3 = buffer.peekInt32();
    
    EXPECT_EQ(peek1, value) << "第 1 次 peek 应返回正确值";
    EXPECT_EQ(peek2, value) << "第 2 次 peek 应返回相同值";
    EXPECT_EQ(peek3, value) << "第 3 次 peek 应返回相同值";
    EXPECT_EQ(buffer.readableBytes(), readableBefore) << "可读字节数不应改变";
    
    // 最后 read 一次，验证值仍然正确
    int32_t readResult = buffer.readInt32();
    EXPECT_EQ(readResult, value);
    EXPECT_EQ(buffer.readableBytes(), 0);
}

// ==================== prependInt32 测试 ====================

/**
 * 测试目标：prependInt32 应在 Buffer 头部插入数据
 * 场景：模拟先写消息体，再在前面加 Length Header
 */
TEST_F(BufferIntegerTest, PrependInt32_InsertsAtBeginning) {
    // 先写入消息体（模拟 Protobuf 序列化后的数据）
    const char* payload = "MessageBody";
    buffer.append(payload, strlen(payload));
    
    // 在前面预置长度字段
    int32_t length = static_cast<int32_t>(strlen(payload));
    buffer.prependInt32(length);
    
    // 验证：先读到的应该是长度
    EXPECT_EQ(buffer.readInt32(), length) << "第一个读取的应该是 prepend 的长度";
    
    // 验证：后读到的应该是消息体
    std::string body = buffer.retrieveAsString(strlen(payload));
    EXPECT_EQ(body, std::string(payload)) << "消息体内容应保持不变";
}

/**
 * 测试目标：连续 prepend 应该逆序插入
 */
TEST_F(BufferIntegerTest, MultiplePrepend_ReverseOrder) {
    buffer.appendInt32(3);  // 最后一个
    buffer.prependInt32(2); // 倒数第二个
    buffer.prependInt32(1); // 第一个
    
    EXPECT_EQ(buffer.readInt32(), 1);
    EXPECT_EQ(buffer.readInt32(), 2);
    EXPECT_EQ(buffer.readInt32(), 3);
}

// ==================== 边界值测试 ====================

/**
 * 测试目标：验证特殊边界值的正确性
 */
TEST_F(BufferIntegerTest, BoundaryValues) {
    struct TestCase {
        int32_t value;
        const char* description;
    };
    
    std::vector<TestCase> testCases = {
        {0, "零值"},
        {-1, "负一"},
        {1, "正一"},
        {INT32_MAX, "最大正数"},
        {INT32_MIN, "最小负数"},
        {0x7FFFFFFF, "0x7FFFFFFF"},
        {-0x7FFFFFFF, "-0x7FFFFFFF"},
    };
    
    for (const auto& tc : testCases) {
        Buffer buf;
        buf.appendInt32(tc.value);
        int32_t result = buf.readInt32();
        EXPECT_EQ(result, tc.value) << "测试失败：" << tc.description;
    }
}

// ==================== 混合操作测试 ====================

/**
 * 测试目标：模拟真实场景的混合操作
 * 场景：构造一个完整的消息包（Length + MsgType + Payload）
 */
TEST_F(BufferIntegerTest, RealWorldScenario_MessagePacket) {
    // 1. 先写入消息类型和负载
    int32_t msgType = 0x1001;  // AgvTelemetry
    const char* payload = "ProtobufSerializedData";
    size_t payloadLen = strlen(payload);
    
    buffer.appendInt32(msgType);
    buffer.append(payload, payloadLen);
    
    // 2. 在最前面加上总长度（4 字节 msgType + payload 长度）
    int32_t totalLength = sizeof(int32_t) + payloadLen;
    buffer.prependInt32(totalLength);
    
    // 3. 模拟接收端解析
    int32_t recvLength = buffer.readInt32();
    EXPECT_EQ(recvLength, totalLength) << "长度字段应正确";
    
    int32_t recvMsgType = buffer.readInt32();
    EXPECT_EQ(recvMsgType, msgType) << "消息类型应正确";
    
    std::string recvPayload = buffer.retrieveAsString(payloadLen);
    EXPECT_EQ(recvPayload, std::string(payload)) << "负载内容应正确";
    
    EXPECT_EQ(buffer.readableBytes(), 0) << "所有数据应被消费完";
}

// ==================== 异常场景测试（依赖 assert）====================

/**
 * 测试目标：验证读取不足时的 assert（在 Debug 模式下生效）
 * 注意：Release 模式下 assert 会被优化掉，此测试仅在 Debug 模式有效
 */
#ifdef NDEBUG
TEST_F(BufferIntegerTest, DISABLED_ReadInt32_InsufficientData_AssertsInDebug) {
#else
TEST_F(BufferIntegerTest, ReadInt32_InsufficientData_AssertsInDebug) {
#endif
    // 只写入 2 字节，不足 4 字节
    buffer.append("AB", 2);
    
    // 在 Debug 模式下应该触发 assert
    EXPECT_DEATH(buffer.readInt32(), "Assertion.*failed");
}

// ==================== 主函数 ====================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
