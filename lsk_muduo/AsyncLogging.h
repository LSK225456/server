#pragma once

#include "noncopyable.h"
#include "LogStream.h"
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace lsk_muduo {

/**
 * @brief 异步日志后端 - 双缓冲技术
 * 
 * 设计要点：
 * 1. 前端：多个业务线程写入 currentBuffer_（临界区短）
 * 2. 后端：单独线程定期（3秒）交换缓冲区并批量写入磁盘
 * 3. 双缓冲：currentBuffer_ + nextBuffer_ + buffers_（待写队列）
 * 4. 备用缓冲：newBuffer1/2 避免频繁分配
 * 
 * 关键优化：
 * - 前端几乎无阻塞（仅 mutex 保护）
 * - 批量写入减少系统调用
 * - 缓冲区复用减少内存分配
 */
class AsyncLogging : noncopyable {
public:
    /**
     * @param basename 日志文件基础名
     * @param rollSize 日志文件滚动大小（默认 500MB）
     * @param flushInterval 刷盘间隔（默认 3 秒）
     */
    AsyncLogging(const std::string& basename,
                 off_t rollSize = 500 * 1000 * 1000,
                 int flushInterval = 3);

    ~AsyncLogging();

    // 前端接口：追加日志（线程安全）
    void append(const char* logline, int len);

    // 启动后端线程
    void start();
    
    // 停止后端线程
    void stop();

private:
    // 后端线程函数
    void threadFunc();

    using Buffer = detail::FixedBuffer<detail::kLargeBuffer>;
    using BufferPtr = std::unique_ptr<Buffer>;
    using BufferVector = std::vector<BufferPtr>;

    const int flushInterval_;         // 刷盘间隔（秒）
    std::atomic<bool> running_;       // 运行标志
    const std::string basename_;      // 日志文件名
    const off_t rollSize_;            // 文件滚动大小

    std::thread thread_;              // 后端线程
    std::mutex mutex_;                // 保护以下成员
    std::condition_variable cond_;    // 唤醒后端线程

    BufferPtr currentBuffer_;         // 当前缓冲（前端写入）
    BufferPtr nextBuffer_;            // 预备缓冲
    BufferVector buffers_;            // 待写入缓冲队列
};

} // namespace lsk_muduo
