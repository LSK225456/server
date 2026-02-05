#include <gtest/gtest.h>
#include "agv_server/codec/LengthHeaderCodec.h"
#include "muduo/net/Buffer.h"

using namespace agv::codec;
using namespace lsk_muduo;

// 测试基本编解码
TEST(CodecTest, BasicEncodeDecode) {
    Buffer buf;
    std::string payload = "hello world";
    uint16_t msgType = 0x1001;
    
    // 编码
    EXPECT_TRUE(LengthHeaderCodec::encode(&buf, msgType, payload));
    EXPECT_EQ(buf.readableBytes(), 8 + payload.size());
    
    // 解码
    uint16_t decodedType = 0;
    std::string decodedPayload;
    EXPECT_TRUE(LengthHeaderCodec::hasCompleteMessage(&buf));
    EXPECT_TRUE(LengthHeaderCodec::decode(&buf, &decodedType, &decodedPayload));
    
    EXPECT_EQ(decodedType, msgType);
    EXPECT_EQ(decodedPayload, payload);
    EXPECT_EQ(buf.readableBytes(), 0u);
}

// 测试半包场景
TEST(CodecTest, IncompleteMessage) {
    Buffer buf;
    std::string payload = "test data";
    
    LengthHeaderCodec::encode(&buf, 0x1001, payload);
    
    // 截断数据，模拟半包
    size_t fullSize = buf.readableBytes();
    std::string data = buf.retrieveAllAsString();
    
    Buffer partialBuf;
    partialBuf.append(data.substr(0, fullSize / 2));
    
    EXPECT_FALSE(LengthHeaderCodec::hasCompleteMessage(&partialBuf));
}

// 测试粘包场景（多条消息）
TEST(CodecTest, MultipleMessages) {
    Buffer buf;
    
    // 编码两条消息
    LengthHeaderCodec::encode(&buf, 0x1001, "message1");
    LengthHeaderCodec::encode(&buf, 0x1002, "message2");
    
    // 解码第一条
    uint16_t type1;
    std::string payload1;
    EXPECT_TRUE(LengthHeaderCodec::decode(&buf, &type1, &payload1));
    EXPECT_EQ(type1, 0x1001);
    EXPECT_EQ(payload1, "message1");
    
    // 解码第二条
    uint16_t type2;
    std::string payload2;
    EXPECT_TRUE(LengthHeaderCodec::decode(&buf, &type2, &payload2));
    EXPECT_EQ(type2, 0x1002);
    EXPECT_EQ(payload2, "message2");
    
    EXPECT_EQ(buf.readableBytes(), 0u);
}

// 测试空负载
TEST(CodecTest, EmptyPayload) {
    Buffer buf;
    
    // 空负载应该编码失败
    EXPECT_FALSE(LengthHeaderCodec::encode(&buf, 0x1001, ""));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
