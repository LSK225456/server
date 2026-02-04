#pragma once

#include "../../muduo/net/Buffer.h"
#include <string>
#include <cstdint>

namespace agv {
namespace codec {

/**
 * @brief 长度头编解码器（工业级实现）
 * 
 * 协议格式：
 * +----------------+------------------+----------------+----------------------+
 * | Length (4字节) | MsgType (2字节)  | Flags (2字节)  | Protobuf Payload (N) |
 * +----------------+------------------+----------------+----------------------+
 * 
 * - Length：总长度（包头8字节 + Protobuf负载），网络字节序（大端）
 * - MsgType：消息类型ID，来自 message_id.h，网络字节序
 * - Flags：预留字段（压缩、加密、优先级等），网络字节序
 * - Protobuf Payload：序列化后的 Protobuf 消息
 */
class LengthHeaderCodec {
public:
    // ========== 常量定义 ==========
    
    /// 包头固定长度（4 + 2 + 2）
    static constexpr size_t kHeaderLen = 8;
    
    /// 最小消息长度（包头 + 至少1字节负载）
    static constexpr size_t kMinMessageLen = kHeaderLen + 1;
    
    /// 最大消息长度（10MB），防止恶意攻击
    static constexpr size_t kMaxMessageLen = 10 * 1024 * 1024;
    
    /// Flags 字段的位定义
    enum FlagBits : uint16_t {
        FLAG_NONE       = 0x0000,
        FLAG_COMPRESSED = 0x0001,
        FLAG_ENCRYPTED  = 0x0002,
        FLAG_PRIORITY   = 0x0004,
    };

    // ========== 编码接口 ==========
    
    static bool encode(Buffer* buf, 
                      uint16_t msgType, 
                      const std::string& protoData,
                      uint16_t flags = FLAG_NONE);

    // ========== 解码接口 ==========
    
    static bool hasCompleteMessage(const Buffer* buf);
    
    static bool decode(Buffer* buf,
                      uint16_t* msgType,
                      std::string* protoData,
                      uint16_t* flags = nullptr);

    static uint32_t peekMessageLength(const Buffer* buf);
};

} // namespace codec
} // namespace agv
