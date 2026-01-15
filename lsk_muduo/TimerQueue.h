#pragma once

#include "noncopyable.h"
#include "Timestamp.h"
#include "Channel.h"
#include "Callbacks.h"

#include <set>
#include <vector>
#include <memory>

class EventLoop;
class Timer;
class TimerId;

// 定时器队列，使用 timerfd 实现统一事件源
class TimerQueue : noncopyable
{
public:
    explicit TimerQueue(EventLoop* loop);
    ~TimerQueue();

    // 添加定时器，线程安全
    // @param cb: 定时器回调
    // @param when: 到期时间
    // @param interval: 重复间隔（秒），0表示一次性定时器
    TimerId addTimer(TimerCallback cb,
                     Timestamp when,
                     double interval);

    // 取消定时器，线程安全
    void cancel(TimerId timerId);

private:
    // 定时器列表的 key-value 对
    typedef std::pair<Timestamp, Timer*> Entry;
    // 定时器列表，按到期时间排序
    typedef std::set<Entry> TimerList;
    // 用于快速查找和取消的活动定时器集合
    typedef std::pair<Timer*, int64_t> ActiveTimer;
    typedef std::set<ActiveTimer> ActiveTimerSet;

    // 在 IO 线程中添加定时器
    void addTimerInLoop(Timer* timer);
    // 在 IO 线程中取消定时器
    void cancelInLoop(TimerId timerId);

    // timerfd 可读时调用
    void handleRead();
    // 获取所有已到期的定时器
    std::vector<Entry> getExpired(Timestamp now);
    // 重置重复定时器
    void reset(const std::vector<Entry>& expired, Timestamp now);

    // 插入定时器到定时器列表
    bool insert(Timer* timer);

    EventLoop* loop_;               // 所属 EventLoop
    const int timerfd_;             // timerfd 文件描述符
    Channel timerfdChannel_;        // timerfd 对应的 Channel
    TimerList timers_;              // 定时器列表

    // 用于 cancel()
    ActiveTimerSet activeTimers_;   // 活动定时器集合，便于通过 Timer* 查找
    bool callingExpiredTimers_;     // 是否正在调用到期的定时器
    ActiveTimerSet cancelingTimers_; // 保存正在取消的定时器
};