#include "ThreadPool.h"

#include <cassert>
#include <cstdio>

ThreadPool::ThreadPool(const std::string& nameArg)
    : name_(nameArg),
      maxQueueSize_(0),
      running_(false)
{
}

ThreadPool::~ThreadPool()
{
    // 确保线程池已停止
    if (running_)
    {
        stop();
    }
}

void ThreadPool::start(int numThreads)
{
    assert(threads_.empty());
    running_ = true;
    threads_.reserve(numThreads);
    
    for (int i = 0; i < numThreads; ++i)
    {
        // 为每个线程创建唯一的名称
        char id[32];
        snprintf(id, sizeof id, "%d", i + 1);
        
        // 创建工作线程，绑定 runInThread 作为线程函数
        threads_.emplace_back(new Thread(
            std::bind(&ThreadPool::runInThread, this), 
            name_ + id));
        threads_[i]->start();
    }
    
    // 特殊情况：如果线程数为0，且有初始化回调，在当前线程执行
    if (numThreads == 0 && threadInitCallback_)
    {
        threadInitCallback_();
    }
}

void ThreadPool::stop()
{
    // 1. 设置停止标志并通知所有线程
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        // 唤醒所有在 notEmpty_ 上等待的消费者线程
        notEmpty_.notify_all();
        // 唤醒所有在 notFull_ 上等待的生产者线程
        notFull_.notify_all();
    }
    
    // 2. 等待所有工作线程退出
    for (auto& thr : threads_)
    {
        thr->join();
    }
}

size_t ThreadPool::queueSize() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

void ThreadPool::run(Task task)
{
    // 特殊情况：没有工作线程，直接在调用者线程执行
    if (threads_.empty())
    {
        task();
    }
    else
    {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 如果是有界队列且已满，等待队列有空位
        // 使用 while 循环防止虚假唤醒
        while (isFull() && running_)
        {
            notFull_.wait(lock);
        }
        
        // 线程池已停止，直接返回
        if (!running_)
        {
            return;
        }
        
        assert(!isFull());
        
        // 任务入队
        queue_.push_back(std::move(task));
        
        // 通知一个等待的消费者线程
        notEmpty_.notify_one();
    }
}

ThreadPool::Task ThreadPool::take()
{
    std::unique_lock<std::mutex> lock(mutex_);
    
    // 等待队列非空
    // 使用 while 循环防止虚假唤醒（spurious wakeup）
    while (queue_.empty() && running_)
    {
        notEmpty_.wait(lock);
    }
    
    Task task;
    if (!queue_.empty())
    {
        // 取出队首任务
        task = std::move(queue_.front());
        queue_.pop_front();
        
        // 如果是有界队列，通知可能在等待的生产者
        if (maxQueueSize_ > 0)
        {
            notFull_.notify_one();
        }
    }
    
    return task;
}

bool ThreadPool::isFull() const
{
    // 调用者必须已持有锁
    // maxQueueSize_ == 0 表示无界队列，永远不满
    return maxQueueSize_ > 0 && queue_.size() >= maxQueueSize_;
}

void ThreadPool::runInThread()
{
    try
    {
        // 执行线程初始化回调（如设置优先级、绑定CPU等）
        if (threadInitCallback_)
        {
            threadInitCallback_();
        }
        
        // 主循环：取任务并执行
        while (running_)
        {
            Task task(take());
            if (task)
            {
                task();
            }
        }
    }
    catch (const std::exception& ex)
    {
        // 捕获标准异常，记录错误并终止
        fprintf(stderr, "exception caught in ThreadPool %s\n", name_.c_str());
        fprintf(stderr, "reason: %s\n", ex.what());
        abort();
    }
    catch (...)
    {
        // 捕获未知异常
        fprintf(stderr, "unknown exception caught in ThreadPool %s\n", name_.c_str());
        throw;  // 重新抛出
    }
}
