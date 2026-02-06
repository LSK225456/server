#ifndef LSK_MUDUO_NET_BUFFER_H
#define LSK_MUDUO_NET_BUFFER_H

#include "../base/copyable.h"
#include <vector>
#include <string>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <arpa/inet.h>  // 添加：提供 htonl, ntohl 等函数

class Buffer : public copyable
{
public:
    static const size_t kCheapPrepend = 8;
    static const size_t kInitialSize = 1024;

    explicit Buffer(size_t initialSize = kInitialSize)
        : buffer_(kCheapPrepend + initialSize)
        , readerIndex_(kCheapPrepend)
        , writerIndex_(kCheapPrepend)
    {}

    size_t readableBytes() const 
    {
        return writerIndex_ - readerIndex_;
    }

    size_t writableBytes() const
    {
        return buffer_.size() - writerIndex_;
    }

    size_t prependableBytes() const
    {
        return readerIndex_;
    }

    const char* peek() const
    {
        return begin() + readerIndex_;
    }

    char* beginWrite()
    {
        return begin() + writerIndex_;
    }

    const char* beginWrite() const
    {
        return begin() + writerIndex_;
    }

    void retrieve(size_t len)
    {
        if (len < readableBytes())
        {
            readerIndex_ += len;
        }
        else
        {
            retrieveAll();
        }
    }

    void retrieveAll()
    {
        readerIndex_ = writerIndex_ = kCheapPrepend;
    }

    std::string retrieveAllAsString()
    {
        return retrieveAsString(readableBytes());
    }

    std::string retrieveAsString(size_t len)
    {
        std::string result(peek(), len);
        retrieve(len);
        return result;
    }

    void ensureWriteableBytes(size_t len)
    {
        if (writableBytes() < len)
        {
            makeSpace(len);
        }
    }

    void append(const char* data, size_t len)
    {
        ensureWriteableBytes(len);
        std::copy(data, data+len, beginWrite());
        writerIndex_ += len;
    }

    ssize_t readFd(int fd, int* saveErrno);
    ssize_t writeFd(int fd, int* saveErrno);

    // ==================== 整数操作接口 ====================
    
    // 网络字节序转换辅助函数
    static int16_t networkToHost16(int16_t net16) {
        return ntohs(static_cast<uint16_t>(net16));
    }
    
    static int32_t networkToHost32(int32_t net32) {
        return ntohl(static_cast<uint32_t>(net32));
    }
    
    static int16_t hostToNetwork16(int16_t host16) {
        return htons(static_cast<uint16_t>(host16));
    }
    
    static int32_t hostToNetwork32(int32_t host32) {
        return htonl(static_cast<uint32_t>(host32));
    }

    /**
     * @brief 查看（但不移动读指针）Buffer 中的 32 位整数
     * @return 主机字节序的 int32_t 值
     * @pre readableBytes() >= sizeof(int32_t)
     * @note 从网络字节序（大端）转换为主机字节序
     */
    int32_t peekInt32() const;

    /**
     * @brief 读取并移除 Buffer 中的 32 位整数
     * @return 主机字节序的 int32_t 值
     * @pre readableBytes() >= sizeof(int32_t)
     * @note 从网络字节序（大端）转换为主机字节序，读指针前进 4 字节
     */
    int32_t readInt32();

    /**
     * @brief 在可写区域追加 32 位整数
     * @param x 主机字节序的 int32_t 值
     * @note 转换为网络字节序（大端）后写入 Buffer
     */
    void appendInt32(int32_t x);

    /**
     * @brief 在可读区域之前预置 32 位整数（用于添加消息头）
     * @param x 主机字节序的 int32_t 值
     * @pre prependableBytes() >= sizeof(int32_t)
     * @note 转换为网络字节序（大端）后写入 Buffer，读指针前移 4 字节
     * @note 典型用途：在消息体前添加 Length Header
     */
    void prependInt32(int32_t x);

    // 扩展性预留：后续可添加 peekInt64、readInt64、appendInt64、prependInt64
    // 以及 uint32_t、uint64_t 等无符号类型的对应方法

    // ========== 新增：批量数据读取方法 ==========
    
    /**
     * @brief 查看指定长度的数据（不移动 readerIndex）
     * @param len 要查看的字节数
     * @return 如果可读字节数 >= len，返回数据；否则返回空字符串
     */
    std::string peek(size_t len) const
    {
        assert(len <= readableBytes());
        return std::string(peek(), len);
    }

    /**
     * @brief 读取指定长度的数据（移动 readerIndex）
     * @param len 要读取的字节数
     * @return 如果可读字节数 >= len，返回数据并移动指针；否则返回空字符串
     */
    std::string read(size_t len)
    {
        assert(len <= readableBytes());
        std::string result(peek(), len);
        retrieve(len);
        return result;
    }

    /**
     * @brief 追加二进制数据（重载版本，支持 std::string）
     */
    void append(const std::string& str)
    {
        append(str.data(), str.size());
    }

    /// 读取 16 位整数（网络字节序 -> 主机字节序）
    int16_t readInt16() {
        int16_t result = peekInt16();
        retrieve(sizeof(int16_t));
        return result;
    }

    /// 查看 16 位整数但不消费（从当前读指针位置）
    int16_t peekInt16() const {
        assert(readableBytes() >= sizeof(int16_t));
        int16_t be16 = 0;
        ::memcpy(&be16, peek(), sizeof(be16));
        return networkToHost16(be16);
    }

    /// 追加 16 位整数（主机字节序 -> 网络字节序）
    void appendInt16(int16_t x) {
        int16_t be16 = hostToNetwork16(x);
        append(&be16, sizeof(be16));
    }

    /// 读取完整的字符串（指定长度）
    std::string read(size_t len) {
        assert(readableBytes() >= len);
        std::string result(peek(), len);
        retrieve(len);
        return result;
    }

    void prependInt16(int16_t x)
    {
        assert(prependableBytes() >= sizeof(int16_t));
        int16_t be16 = htobe16(x);
        readerIndex_ -= sizeof(int16_t);
        ::memcpy(begin() + readerIndex_, &be16, sizeof(be16));  // 使用 ::memcpy
    }

    // 添加 clear() 方法（如果不存在）
    void clear()
    {
        retrieveAll();
    }

private:

    char* begin()
    {
        return &*buffer_.begin();
    }

    const char* begin() const
    {
        return &*buffer_.begin();
    }

    void makeSpace(size_t len)
    {
        if (writableBytes() + readableBytes() < len + kCheapPrepend)
        {
            buffer_.resize(writerIndex_ + len);
        }
        else
        {
            size_t readable = readableBytes();
            std::copy(begin() + readerIndex_,
                                begin() + writerIndex_,
                                begin() + kCheapPrepend);
            readerIndex_ = kCheapPrepend;
            writerIndex_ = readerIndex_ + readable;
        }
    }

    std::vector<char> buffer_;
    size_t readerIndex_;
    size_t writerIndex_;
};

}  // namespace lsk_muduo

#endif  // LSK_MUDUO_NET_BUFFER_H