#pragma once

#include "noncopyable.h"
#include "Thread.h"

#include <functional>
#include <vector>
#include <deque>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <string>
#include <atomic>

/**
 * ThreadPool - 通用计算线程池
 * 
 * 与 EventLoopThreadPool 的区别：
 * - EventLoopThreadPool: IO线程池，每个线程运行一个 EventLoop 处理网络事件
 * - ThreadPool: 计算线程池，线程从任务队列取任务执行，适合CPU密集计算
 * 
 * 设计要点：
 * 1. 支持有界/无界队列（maxQueueSize_ 控制）
 * 2. 生产者-消费者模型，使用条件变量同步
 * 3. 支持优雅关闭，等待所有任务完成
 * 
 * 典型使用场景：
 * - 耗时的计算任务（如加密、压缩）
 * - 阻塞的IO操作（如数据库查询）
 * - 避免这些任务阻塞IO线程
 * 
 * 使用示例：
 * @code
 *   ThreadPool pool("ComputePool");
 *   pool.setMaxQueueSize(100);  // 可选：设置有界队列
 *   pool.start(4);              // 启动4个工作线程
 *   
 *   pool.run([]{
 *       // 耗时计算任务
 *   });
 *   
 *   pool.stop();  // 优雅关闭，等待任务完成
 * @endcode
 * 
 * 面试考点：
 * - IO线程池 vs 计算线程池分离的必要性
 * - 任务队列设计：有界 vs 无界、防止任务堆积
 * - 线程池优雅关闭：通知所有线程、等待任务完成
 * - 条件变量的虚假唤醒处理（while循环检查条件）
 */
class ThreadPool : noncopyable
{
public:
    using Task = std::function<void()>;

    explicit ThreadPool(const std::string& nameArg = std::string("ThreadPool"));
    ~ThreadPool();

    // ============ 配置接口（必须在 start() 之前调用）============
    
    /**
     * 设置任务队列最大长度
     * @param maxSize 最大队列长度，0表示无界队列
     * 
     * 有界队列的作用：
     * - 防止任务堆积导致内存耗尽
     * - 当队列满时，run() 会阻塞生产者
     */
    void setMaxQueueSize(size_t maxSize) { maxQueueSize_ = maxSize; }
    
    /**
     * 设置线程初始化回调
     * 每个工作线程启动时会先调用此回调
     * 可用于设置线程优先级、绑定CPU核心等
     */
    void setThreadInitCallback(const Task& cb) { threadInitCallback_ = cb; }

    // ============ 生命周期管理 ============
    
    /**
     * 启动线程池
     * @param numThreads 工作线程数量
     * 
     * 特殊情况：numThreads=0 时，任务在调用者线程直接执行
     */
    void start(int numThreads);
    
    /**
     * 停止线程池
     * 
     * 行为：
     * 1. 设置 running_ = false
     * 2. 唤醒所有等待的线程
     * 3. 等待所有线程退出（join）
     * 
     * 注意：队列中未执行的任务将被丢弃
     */
    void stop();

    // ============ 任务提交 ============
    
    /**
     * 提交任务到线程池
     * @param task 要执行的任务
     * 
     * 行为：
     * - 如果线程池为空（numThreads=0），任务在当前线程直接执行
     * - 如果队列未满，任务入队并通知一个工作线程
     * - 如果队列已满（有界队列），调用者阻塞直到队列有空位
     * - 如果线程池已停止，任务被忽略
     */
    void run(Task task);

    // ============ 查询接口 ============
    
    const std::string& name() const { return name_; }
    
    size_t queueSize() const;

private:
    /**
     * 判断队列是否已满（用于有界队列）
     * 调用前必须持有锁
     */
    bool isFull() const;
    
    /**
     * 工作线程的主函数
     * 循环：取任务 -> 执行任务 -> 取任务 -> ...
     */
    void runInThread();
    
    /**
     * 从队列中取出一个任务
     * 如果队列为空，会阻塞等待
     * @return 取出的任务，如果线程池停止则可能返回空任务
     */
    Task take();

    // ============ 成员变量 ============
    
    mutable std::mutex mutex_;              // 保护队列和状态
    std::condition_variable notEmpty_;      // 队列非空条件（消费者等待）
    std::condition_variable notFull_;       // 队列非满条件（生产者等待）
    
    std::string name_;                      // 线程池名称
    Task threadInitCallback_;               // 线程初始化回调
    std::vector<std::unique_ptr<Thread>> threads_;  // 工作线程列表
    std::deque<Task> queue_;                // 任务队列
    size_t maxQueueSize_;                   // 队列最大长度（0表示无界）
    std::atomic_bool running_;              // 线程池运行状态
};
