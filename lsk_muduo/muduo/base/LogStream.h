#pragma once

#include "noncopyable.h"
#include <string>
#include <cstring>

namespace lsk_muduo {

const int kSmallBuffer = 4000;      // LogStream用（单条日志）
const int kLargeBuffer = 4000 * 1000;       // AsyncLogging用（4MB批量）

template<int SIZE>
class FixedBuffer : noncopyable {
public:
    FixedBuffer() : cur_(data_) {}

    void append(const char* buf, size_t len) {
        if (static_cast<size_t>(avail()) > len) {   // 检查剩余空间
            memcpy(cur_, buf, len);                 // 内存拷贝
            cur_ += len;                            // 移动指针
        }
        // 注意：如果空间不够，什么都不做（日志不能阻塞业务）
    }

    const char* data() const { return data_; }      // 获取已写入的数据
    int length() const { return static_cast<int>(cur_ - data_); }

    char* current() { return cur_; }
    int avail() const { return static_cast<int>(end() - cur_); }
    void add(size_t len) { cur_ += len; }

    void reset() { cur_ = data_; }      // 重置（清空）
    void bzero() { memset(data_, 0, sizeof(data_)); }

    std::string toString() const { return std::string(data_, length()); }

private:
    const char* end() const { return data_ + sizeof(data_); }
    char data_[SIZE];       // 栈上或对象内固定大小数组
    char* cur_;                 // 当前写入位置
};

class LogStream : noncopyable {
public:
    using Buffer = FixedBuffer<kSmallBuffer>;

    LogStream& operator<<(bool v) {
        buffer_.append(v ? "1" : "0", 1);
        return *this;
    }

    LogStream& operator<<(short);
    LogStream& operator<<(unsigned short);
    LogStream& operator<<(int);
    LogStream& operator<<(unsigned int);
    LogStream& operator<<(long);
    LogStream& operator<<(unsigned long);
    LogStream& operator<<(long long);
    LogStream& operator<<(unsigned long long);

    LogStream& operator<<(float v) {
        *this << static_cast<double>(v);
        return *this;
    }
    LogStream& operator<<(double);

    LogStream& operator<<(char v) {
        buffer_.append(&v, 1);
        return *this;
    }

    LogStream& operator<<(const char* str) {
        if (str) {
            buffer_.append(str, strlen(str));
        } else {
            buffer_.append("(null)", 6);
        }
        return *this;
    }

    LogStream& operator<<(const unsigned char* str) {
        return operator<<(reinterpret_cast<const char*>(str));
    }

    LogStream& operator<<(const std::string& v) {
        buffer_.append(v.c_str(), v.size());
        return *this;
    }

    void append(const char* data, int len) { buffer_.append(data, len); }
    const Buffer& buffer() const { return buffer_; }
    void resetBuffer() { buffer_.reset(); }

private:
    template<typename T>
    void formatInteger(T);

    Buffer buffer_;
    static const int kMaxNumericSize = 48;
};

} // namespace lsk_muduo
