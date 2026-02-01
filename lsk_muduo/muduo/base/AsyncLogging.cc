#include "AsyncLogging.h"
#include "LogFile.h"
#include <cassert>

namespace lsk_muduo {

AsyncLogging::AsyncLogging(const std::string& basename,
                           off_t rollSize,
                           int flushInterval)
    : flushInterval_(flushInterval),
      running_(false),
      basename_(basename),
      rollSize_(rollSize),
      currentBuffer_(new Buffer),
      nextBuffer_(new Buffer) {
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
    if (currentBuffer_->avail() > len) {
        // 当前缓冲区够用
        currentBuffer_->append(logline, len);
    } else {
        // 当前缓冲区已满，放入待写队列
        buffers_.push_back(std::move(currentBuffer_));

        if (nextBuffer_) {
            // 使用预备缓冲区
            currentBuffer_ = std::move(nextBuffer_);
        } else {
            // 预备缓冲区也用完了，分配新的（很少发生）
            currentBuffer_.reset(new Buffer);
        }
        currentBuffer_->append(logline, len);
        cond_.notify_one();  // 通知后端线程有数据可写
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
    assert(running_ == true);
    LogFile output(basename_, rollSize_, false);  // 后端单线程，无需加锁
    
    // 后端持有两个备用缓冲区，用于与前端交换
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

        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (buffers_.empty()) {
                // 没有数据，等待一段时间
                cond_.wait_for(lock, std::chrono::seconds(flushInterval_));
            }
            
            // 无论是否超时，都将当前缓冲区放入待写队列
            buffers_.push_back(std::move(currentBuffer_));
            currentBuffer_ = std::move(newBuffer1);
            
            // 交换队列，减少临界区时间
            buffersToWrite.swap(buffers_);
            
            if (!nextBuffer_) {
                nextBuffer_ = std::move(newBuffer2);
            }
        }

        assert(!buffersToWrite.empty());

        // 防止日志堆积：超过25个缓冲，丢弃多余的
        if (buffersToWrite.size() > 25) {
            char buf[256];
            snprintf(buf, sizeof(buf), 
                     "Dropped log messages, %zd larger buffers\n",
                     buffersToWrite.size() - 2);
            fputs(buf, stderr);
            output.append(buf, static_cast<int>(strlen(buf)));
            buffersToWrite.erase(buffersToWrite.begin() + 2, buffersToWrite.end());
        }

        // 写入日志文件
        for (const auto& buffer : buffersToWrite) {
            output.append(buffer->data(), buffer->length());
        }

        // 只保留两个缓冲区复用，其余释放
        if (buffersToWrite.size() > 2) {
            buffersToWrite.resize(2);
        }

        // 归还缓冲区给后端
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

    // 退出前刷盘
    output.flush();
}

} // namespace lsk_muduo
