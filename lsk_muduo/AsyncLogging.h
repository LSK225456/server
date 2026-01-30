#pragma once

#include "noncopyable.h"
#include "LogStream.h"
#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

namespace lsk_muduo {

class AsyncLogging : noncopyable {
public:
    AsyncLogging(const std::string& basename,
                 off_t rollSize,
                 int flushInterval = 3);
    ~AsyncLogging();

    // 前端线程调用，将日志追加到缓冲区
    void append(const char* logline, int len);

    void start();
    void stop();

private:
    void threadFunc();

    using Buffer = FixedBuffer<kLargeBuffer>;
    using BufferPtr = std::unique_ptr<Buffer>;
    using BufferVector = std::vector<BufferPtr>;

    const int flushInterval_;
    std::atomic<bool> running_;
    const std::string basename_;
    const off_t rollSize_;
    std::thread thread_;
    
    std::mutex mutex_;
    std::condition_variable cond_;
    
    BufferPtr currentBuffer_;   // 当前缓冲区
    BufferPtr nextBuffer_;      // 预备缓冲区
    BufferVector buffers_;      // 待写入的已满缓冲区队列
};

} // namespace lsk_muduo
