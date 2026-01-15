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

// 它负责维护所有定时器的顺序,定时器队列，使用 timerfd 实现统一事件源
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

    // 取消定时器，线程安全,清理所有未执行的 Timer 内存
    void cancel(TimerId timerId);

private:
    // 类型定义：Entry
    // 为什么用 pair？因为 std::set 需要排序。
    // Key (first): Timestamp，按到期时间排序，时间越早越靠前。
    // Value (second): Timer*，当时间相同时，利用指针地址区分不同对象。
    // 这样保证了 set 中不会有重复元素，且 begin() 永远是最早到期的那个。
    typedef std::pair<Timestamp, Timer*> Entry;

    // 定时器列表，按到期时间排序,1个红黑树结构的集合
    typedef std::set<Entry> TimerList;

    // 用于快速查找和取消的活动定时器集合
    // 这是一个辅助索引，用于 cancel 操作。
    // Key (first): Timer*，按内存地址排序。
    // Value (second): int64_t，序列号。
    typedef std::pair<Timer*, int64_t> ActiveTimer;
    typedef std::set<ActiveTimer> ActiveTimerSet;

    // 在 IO 线程中添加定时器
    void addTimerInLoop(Timer* timer);
    // 在 IO 线程中取消定时器
    void cancelInLoop(TimerId timerId);

    // timerfd 可读时调用
    void handleRead();
    // 核心算法：从 timers_ 中移除所有已到期的定时器，并返回它们
    std::vector<Entry> getExpired(Timestamp now);
    // 核心算法：处理完回调后，对于重复定时器，重新计算时间并塞回 timers_
    void reset(const std::vector<Entry>& expired, Timestamp now);

    // 插入定时器到定时器列表
    bool insert(Timer* timer);

    EventLoop* loop_;               // 所属 EventLoop
    const int timerfd_;             // timerfd 文件描述符
    Channel timerfdChannel_;        // timerfd 对应的 Channel
    TimerList timers_;              // 定时器列表

    // 用于 cancel()
    // 辅助容器：按地址排序的定时器集合。作用：当用户调用 cancel(TimerId) 时，我们只有 Timer指针。
    // 在 timers_ (按时间排) 中找指针是 O(N) 的，太慢。在 activeTimers_ (按指针排) 中找指针是 O(log N) 的，很快。
    ActiveTimerSet activeTimers_;   // 活动定时器集合，便于通过 Timer* 查找

    
    bool callingExpiredTimers_;     // 是否正在调用到期的定时器
    ActiveTimerSet cancelingTimers_; // 保存正在取消的定时器
};