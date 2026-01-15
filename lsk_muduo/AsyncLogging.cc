#include "AsyncLogging.h"
#include "LogFile.h"
#include "Timestamp.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>

namespace lsk_muduo {

AsyncLogging::AsyncLogging(const std::string& basename,
                           off_t rollSize,
                           int flushInterval)
    : flushInterval_(flushInterval),
      running_(false),
      basename_(basename),
      rollSize_(rollSize),
      thread_(),
      mutex_(),
      cond_(),
      currentBuffer_(new Buffer),
      nextBuffer_(new Buffer),
      buffers_() {
    currentBuffer_->bzero();
    nextBuffer_->bzero();
    buffers_.reserve(16);
}

AsyncLogging::~AsyncLogging() {
    if (running_) {
        stop();
    }
}

void AsyncLogging::append(const char* logline, int len) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 当前缓冲区空间足够，直接追加
    if (currentBuffer_->avail() > len) {
        currentBuffer_->append(logline, len);
    } else {
        // 当前缓冲区已满，放入待写队列
        buffers_.push_back(std::move(currentBuffer_));

        // 使用预备缓冲或新建缓冲
        if (nextBuffer_) {
            currentBuffer_ = std::move(nextBuffer_);
        } else {
            currentBuffer_.reset(new Buffer); // 极少发生
        }
        currentBuffer_->append(logline, len);
        
        // 通知后端线程有数据可写
        cond_.notify_one();
    }
}

void AsyncLogging::start() {
    running_ = true;
    thread_ = std::thread(&AsyncLogging::threadFunc, this);
}

void AsyncLogging::stop() {
    running_ = false;
    cond_.notify_one();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void AsyncLogging::threadFunc() {
    assert(running_);
    
    LogFile output(basename_, rollSize_, flushInterval_);
    
    // 准备两个备用缓冲区（供前端使用）
    BufferPtr newBuffer1(new Buffer);
    BufferPtr newBuffer2(new Buffer);
    newBuffer1->bzero();
    newBuffer2->bzero();

    BufferVector buffersToWrite;
    buffersToWrite.reserve(16);

    while (running_) {
        assert(newBuffer1 && newBuffer1->length() == 0);
        assert(newBuffer2 && newBuffer2->length() == 0);
        assert(buffersToWrite.empty());

        // 临界区：交换缓冲区
        {
            std::unique_lock<std::mutex> lock(mutex_);
            
            if (buffers_.empty()) {
                // 等待前端数据或超时（超时保证定期刷盘）
                cond_.wait_for(lock, std::chrono::seconds(flushInterval_));
            }

            // 将当前缓冲移入待写队列（即使未满也刷盘）
            buffers_.push_back(std::move(currentBuffer_));
            currentBuffer_ = std::move(newBuffer1);

            // 交换待写队列到本地
            buffersToWrite.swap(buffers_);

            // 补充预备缓冲
            if (!nextBuffer_) {
                nextBuffer_ = std::move(newBuffer2);
            }
        }

        assert(!buffersToWrite.empty());

        // 异常情况：消息堆积过多，丢弃部分
        if (buffersToWrite.size() > 25) {
            char buf[256];
            // 获取当前时间
            time_t now = time(NULL);
            struct tm tm;
            localtime_r(&now, &tm);
            char timebuf[32];
            strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm);
            
            snprintf(buf, sizeof(buf), 
                     "Dropped log messages at %s, %zd larger buffers\n",
                     timebuf,
                     buffersToWrite.size() - 2);
            fputs(buf, stderr);
            output.append(buf, static_cast<int>(strlen(buf)));
            
            // 只保留前两个缓冲
            buffersToWrite.erase(buffersToWrite.begin() + 2, buffersToWrite.end());
        }

        // 批量写入磁盘
        for (const auto& buffer : buffersToWrite) {
            output.append(buffer->data(), buffer->length());
        }

        // 缓冲区复用：保留两个供前端使用
        if (buffersToWrite.size() > 2) {
            buffersToWrite.resize(2);
        }

        if (!newBuffer1) {
            assert(!buffersToWrite.empty());
            newBuffer1 = std::move(buffersToWrite.back());
            buffersToWrite.pop_back();
            newBuffer1->reset();
        }

        if (!newBuffer2) {
            assert(!buffersToWrite.empty());
            newBuffer2 = std::move(buffersToWrite.back());
            buffersToWrite.pop_back();
            newBuffer2->reset();
        }

        buffersToWrite.clear();
        output.flush();
    }

    output.flush();
}

} // namespace lsk_muduo
