#pragma once

#include "noncopyable.h"
#include <string.h>
#include <string>

namespace lsk_muduo {

namespace detail {

const int kSmallBuffer = 4000;        // 小缓冲区：4KB，用于前端日志
const int kLargeBuffer = 4000 * 1000; // 大缓冲区：4MB，用于后端批量写入

/**
 * @brief 固定大小的缓冲区模板类
 * 
 * 特点：
 * - 栈上分配，避免动态内存分配开销
 * - 支持高效 append 操作
 * - 提供字符串视图接口
 */
template<int SIZE>
class FixedBuffer : noncopyable {
public:
    FixedBuffer() : cur_(data_) {}

    ~FixedBuffer() = default;

    // 追加数据到缓冲区
    void append(const char* buf, size_t len) {
        if (avail() > static_cast<int>(len)) {
            memcpy(cur_, buf, len);
            cur_ += len;
        }
    }

    // 获取缓冲区起始地址
    const char* data() const { return data_; }
    
    // 获取当前已使用长度
    int length() const { return static_cast<int>(cur_ - data_); }

    // 获取当前写位置指针
    char* current() { return cur_; }
    
    // 获取剩余可用空间
    int avail() const { return static_cast<int>(end() - cur_); }
    
    // 移动写指针（外部直接写入后调用）
    void add(size_t len) { cur_ += len; }

    // 重置缓冲区
    void reset() { cur_ = data_; }
    
    // 清零缓冲区
    void bzero() { memset(data_, 0, sizeof(data_)); }

    // 转为字符串（调试用）
    std::string toString() const { return std::string(data_, length()); }

private:
    const char* end() const { return data_ + sizeof(data_); }

    char data_[SIZE];  // 固定大小数组
    char* cur_;        // 当前写位置
};

} // namespace detail

/**
 * @brief 日志流类 - 支持流式格式化输出
 * 
 * 使用场景：
 * - LogStream << "value: " << 42 << ", str: " << "hello";
 * - 避免 snprintf 的性能开销
 * - 类型安全的格式化
 */
class LogStream : noncopyable {
public:
    using Buffer = detail::FixedBuffer<detail::kSmallBuffer>;

    LogStream() = default;

    // 字符串追加（C风格）
    LogStream& operator<<(const char* str) {
        if (str) {
            buffer_.append(str, strlen(str));
        } else {
            buffer_.append("(null)", 6);
        }
        return *this;
    }

    // 字符串追加（std::string）
    LogStream& operator<<(const std::string& str) {
        buffer_.append(str.c_str(), str.size());
        return *this;
    }

    // 布尔值
    LogStream& operator<<(bool v) {
        buffer_.append(v ? "true" : "false", v ? 4 : 5);
        return *this;
    }

    // 整数类型
    LogStream& operator<<(short);
    LogStream& operator<<(unsigned short);
    LogStream& operator<<(int);
    LogStream& operator<<(unsigned int);
    LogStream& operator<<(long);
    LogStream& operator<<(unsigned long);
    LogStream& operator<<(long long);
    LogStream& operator<<(unsigned long long);

    // 浮点数
    LogStream& operator<<(float v) {
        *this << static_cast<double>(v);
        return *this;
    }
    LogStream& operator<<(double);

    // 字符
    LogStream& operator<<(char c) {
        buffer_.append(&c, 1);
        return *this;
    }

    // 指针（输出十六进制地址）
    LogStream& operator<<(const void* p);

    // 获取缓冲区
    const Buffer& buffer() const { return buffer_; }
    void resetBuffer() { buffer_.reset(); }

private:
    // 整数转字符串的高效实现
    template<typename T>
    void formatInteger(T);

    Buffer buffer_;
    static const int kMaxNumericSize = 32;
};

} // namespace lsk_muduo
