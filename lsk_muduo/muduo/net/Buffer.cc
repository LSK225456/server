#include "Buffer.h"

#include <errno.h>
#include <sys/uio.h>
#include <unistd.h>
#include <cstring>  // 添加此头文件，提供 memcpy


namespace lsk_muduo {
ssize_t Buffer::readFd(int fd, int *saveErrno)
{
    char extrabuf[65536] = {0};

    struct iovec vec[2];

    const size_t writable = writableBytes();
    vec[0].iov_base = begin() + writerIndex_;
    vec[0].iov_len = writable;

    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof extrabuf;

    const int iovcnt = (writable < sizeof extrabuf) ? 2 : 1;
    const ssize_t n = ::readv(fd, vec, iovcnt);
    if (n < 0)
    {
        *saveErrno =errno;
    }
    else if (n <= static_cast<ssize_t>(writable))
    {
        writerIndex_ += n;
    }
    else
    {
        writerIndex_ = buffer_.size();
        append(extrabuf, n - writable);
    }
    return n;
}

ssize_t Buffer::writeFd(int fd, int *saveErrno)
{
    ssize_t n = ::write(fd, peek(), readableBytes());
    if (n < 0)
    {
        *saveErrno = errno;
    }
    return n;
}

// ==================== 整数操作实现 ====================

int32_t Buffer::peekInt32() const
{
    // 断言：确保有足够的可读字节
    assert(readableBytes() >= sizeof(int32_t));
    
    // 从 Buffer 中读取 4 字节（网络字节序，大端）
    int32_t be32 = 0;
    std::memcpy(&be32, peek(), sizeof(int32_t));
    
    // 从网络字节序（大端）转换为主机字节序
    return networkToHost32(be32);
}

int32_t Buffer::readInt32()
{
    // 先 peek 获取值
    int32_t result = peekInt32();
    
    // 移动读指针，消费掉这 4 字节
    retrieve(sizeof(int32_t));
    
    return result;
}

void Buffer::appendInt32(int32_t x)
{
    // 从主机字节序转换为网络字节序（大端）
    int32_t be32 = hostToNetwork32(x);
    
    // 追加到可写区域
    append(reinterpret_cast<const char*>(&be32), sizeof(be32));
}

void Buffer::prependInt32(int32_t x)
{
    // 断言：确保有足够的预置空间（kCheapPrepend 设计为 8 字节，足够放 int32）
    assert(prependableBytes() >= sizeof(int32_t));
    
    // 从主机字节序转换为网络字节序（大端）
    int32_t be32 = hostToNetwork32(x);
    
    // 读指针前移 4 字节
    readerIndex_ -= sizeof(int32_t);
    
    // 在新的读指针位置写入数据
    std::memcpy(begin() + readerIndex_, &be32, sizeof(be32));
}
} // namespace lsk_muduo