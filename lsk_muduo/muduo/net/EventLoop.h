#pragma once

#include <functional>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>

#include "../base/noncopyable.h"
#include "../base/Timestamp.h"
#include "../base/CurrentThread.h"
namespace lsk_muduo {
class Channel;
class Poller;
class TimerQueue;  
class TimerId;     

class EventLoop : noncopyable
{
public:
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();

    void loop();
    void quit();

    Timestamp pollReturnTime() const { return pollReturnTime_; }

    void runInLoop(Functor cb);
    void queueInLoop(Functor cb);

    void wakeup();

    void updateChannel(Channel *channel);
    void removeChannel(Channel *channel);
    bool hasChannel(Channel *channel);

    bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }

    void assertInLoopThread()
    {
        if (!isInLoopThread())
        {
            // 可以添加断言或日志
        }
    }

    // ============ 新增：定时器接口 ============
    // 在指定时间运行回调
    TimerId runAt(Timestamp time, Functor cb);
    // 在 delay 秒后运行回调
    TimerId runAfter(double delay, Functor cb);
    // 每隔 interval 秒运行一次回调
    TimerId runEvery(double interval, Functor cb);
    // 取消定时器
    void cancel(TimerId timerId);

private:
    void handleRead();
    void doPendingFunctors();

    Timestamp pollReturnTime_;

    using ChannelList = std::vector<Channel*>;

    std::atomic_bool looping_;
    std::atomic_bool quit_;

    const pid_t threadId_;
    std::unique_ptr<Poller> poller_;

    int wakeupFd_;
    std::unique_ptr<Channel> wakeupChannel_;

    ChannelList activeChannels_;

    std::atomic_bool callingPendingFunctors_;
    std::vector<Functor> pendingFunctors_;
    std::mutex mutex_;

    std::unique_ptr<TimerQueue> timerQueue_;

};
}