#include "LengthHeaderCodec.h"
#include "../../muduo/base/Logger.h"
#include <arpa/inet.h>
#include <iomanip>  // 添加：std::hex, std::dec

namespace agv {
namespace codec {

bool LengthHeaderCodec::encode(lsk_muduo::Buffer* buf,
                               uint16_t msgType,
                               const std::string& protoData,
                               uint16_t flags)
{
    if (!buf) {
        LOG_ERROR << "LengthHeaderCodec::encode - Invalid buffer pointer";
        return false;
    }

    const uint32_t totalLen = static_cast<uint32_t>(kHeaderLen + protoData.size());
    
    if (totalLen > kMaxMessageLen) {
        LOG_ERROR << "LengthHeaderCodec::encode - Message too large: " << totalLen;
        return false;
    }

    buf->ensureWriteableBytes(totalLen);
    buf->appendInt32(static_cast<int32_t>(totalLen));
    buf->appendInt16(static_cast<int16_t>(msgType));
    buf->appendInt16(static_cast<int16_t>(flags));
    buf->append(protoData);

    return true;
}

bool LengthHeaderCodec::hasCompleteMessage(const lsk_muduo::Buffer* buf)
{
    if (!buf || buf->readableBytes() < kHeaderLen) {
        return false;
    }

    const uint32_t totalLen = static_cast<uint32_t>(buf->peekInt32());

    if (totalLen < kMinMessageLen || totalLen > kMaxMessageLen) {
        return false;
    }

    return buf->readableBytes() >= totalLen;
}

uint32_t LengthHeaderCodec::peekMessageLength(const lsk_muduo::Buffer* buf)
{
    if (!buf || buf->readableBytes() < kHeaderLen) {
        return 0;
    }

    const uint32_t totalLen = static_cast<uint32_t>(buf->peekInt32());
    
    if (totalLen < kMinMessageLen || totalLen > kMaxMessageLen) {
        return 0;
    }

    return totalLen;
}

bool LengthHeaderCodec::decode(lsk_muduo::Buffer* buf,
                               uint16_t* msgType,
                               std::string* protoData,
                               uint16_t* flags)
{
    if (!buf || !msgType || !protoData) {
        LOG_ERROR << "LengthHeaderCodec::decode - Invalid parameters";
        return false;
    }

    if (!hasCompleteMessage(buf)) {
        return false;
    }

    const uint32_t totalLen = static_cast<uint32_t>(buf->readInt32());
    const uint16_t type = static_cast<uint16_t>(buf->readInt16());
    const uint16_t flag = static_cast<uint16_t>(buf->readInt16());

    const size_t payloadLen = totalLen - kHeaderLen;
    *protoData = buf->read(payloadLen);
    *msgType = type;
    
    if (flags) {
        *flags = flag;
    }

    return true;
}

} // namespace codec
} // namespace agv
