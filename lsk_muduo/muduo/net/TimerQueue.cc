#include "TimerQueue.h"
#include "../base/Logger.h"
#include "EventLoop.h"
#include "Timer.h"
#include "TimerId.h"

#include <sys/timerfd.h>
#include <unistd.h>
#include <string.h>
#include <cassert>       
#include <algorithm>   
namespace lsk_muduo {

// 向操作系统申请一个定时器文件描述符
int createTimerfd()
{
    int timerfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timerfd < 0)
    {
        LOG_FATAL << "Failed in timerfd_create";
    }
    return timerfd;
}

// 计算超时时刻与当前时间的时间差,计算“现在”到“目标时间 when”还有多久，转换成系统API需要的 timespec 结构
struct timespec howMuchTimeFromNow(Timestamp when)
{
    int64_t microseconds = when.microSecondsSinceEpoch()
                           - Timestamp::now().microSecondsSinceEpoch();         // 计算微秒差值

    // 【细节防护】如果不加这个判断，当 microseconds < 0 时（即定时器已经过期），传递给 timerfd_settime 可能会导致错误或立即触发大量事件。
    // 这里设置最小延时为 100 微秒，保证系统调用的稳定性。                       
    if (microseconds < 100)
    {
        microseconds = 100;  // 最少 100 微秒
    }

    struct timespec ts;
    ts.tv_sec = static_cast<time_t>(
        microseconds / Timestamp::kMicroSecondsPerSecond);      // 秒部分
    ts.tv_nsec = static_cast<long>(
        (microseconds % Timestamp::kMicroSecondsPerSecond) * 1000);     // 纳秒部分
    return ts;
}

// 作用：当 epoll 告诉你 timerfd 可读时，必须把里面的数据读走。
// 如果不读，水平触发模式下 epoll 会一直通知你，导致死循环。
void readTimerfd(int timerfd, Timestamp now)
{
    uint64_t howmany;
    ssize_t n = ::read(timerfd, &howmany, sizeof howmany);
    LOG_INFO << "TimerQueue::handleRead() " << howmany << " at " << now.toString();
    if (n != sizeof howmany)
    {
        LOG_ERROR << "TimerQueue::handleRead() reads " << n << " bytes instead of 8";
    }
}

// 重置定时器的超时时间。这里的 expiration 是“下一个最近要触发的定时器”的时间。
void resetTimerfd(int timerfd, Timestamp expiration)
{
    struct itimerspec newValue;
    struct itimerspec oldValue;
    memset(&newValue, 0, sizeof newValue);
    memset(&oldValue, 0, sizeof oldValue);
    newValue.it_value = howMuchTimeFromNow(expiration);     // it_value: 首次到期时间

    //从现在开始倒计时，it_value 秒后把 timerfd 这个文件变成‘可读’状态，以此来叫醒我。
    int ret = ::timerfd_settime(timerfd, 0, &newValue, &oldValue);
    if (ret)
    {
        LOG_ERROR << "timerfd_settime() failed";
    }
}

TimerQueue::TimerQueue(EventLoop* loop)
    : loop_(loop),
      timerfd_(createTimerfd()),
      timerfdChannel_(loop, timerfd_),
      timers_(),
      callingExpiredTimers_(false)
{
    // 当 timerfd 到期 -> Channel::handleEvent -> TimerQueue::handleRead 只有时间到期了，这个 fd 才会变得‘可读’，回调才会被触发。
    timerfdChannel_.setReadCallback(
        std::bind(&TimerQueue::handleRead, this));
    // 开启读事件监听（将 timerfd 注册到 epoll）
    timerfdChannel_.enableReading();
}

TimerQueue::~TimerQueue()
{
    timerfdChannel_.disableAll();
    timerfdChannel_.remove();
    ::close(timerfd_);
    // 删除所有定时器
    for (const Entry& timer : timers_)
    {
        delete timer.second;
    }
}

TimerId TimerQueue::addTimer(TimerCallback cb,
                              Timestamp when,
                              double interval)
{
    // 1. 在堆上创建一个新的 Timer 对象  这个对象直到 cancel 或 执行完毕 才会被 delete
    Timer* timer = new Timer(std::move(cb), when, interval);
    loop_->runInLoop(
        std::bind(&TimerQueue::addTimerInLoop, this, timer));
    return TimerId(timer, timer->sequence());       // 返回 TimerId 给用户，用于可能的 cancel
}

void TimerQueue::cancel(TimerId timerId)
{
    loop_->runInLoop(
        std::bind(&TimerQueue::cancelInLoop, this, timerId));
}

void TimerQueue::addTimerInLoop(Timer* timer)
{
    loop_->assertInLoopThread();
    // 尝试插入到 timers_ 列表中,insert 返回 bool：如果插入的这个新定时器，比之前所有定时器都更早到期，返回 true
    bool earliestChanged = insert(timer);

    if (earliestChanged)
    {
        // 如果新插入的定时器排在了最前面，说明我们需要修改内核的 timerfd 设置，让它在更早的时间唤醒我们。
        resetTimerfd(timerfd_, timer->expiration());
    }
}

void TimerQueue::cancelInLoop(TimerId timerId)
{
    loop_->assertInLoopThread();
    assert(timers_.size() == activeTimers_.size());
    ActiveTimer timer(timerId.timer_, timerId.sequence_);   // 构造一个用于查找的 Key
    ActiveTimerSet::iterator it = activeTimers_.find(timer);    // 利用 activeTimers_ 按指针排序的特性，快速找到
    if (it != activeTimers_.end())
    {
        // Case A: 定时器还在队列中（未到期）
        size_t n = timers_.erase(Entry(it->first->expiration(), it->first));
        assert(n == 1); (void)n;
        delete it->first;
        activeTimers_.erase(it);
    }
    else if (callingExpiredTimers_)
    {
        // Case B: 定时器不在队列中，但正在“处理过期任务”的状态
        // 如果正在调用定时器回调，将其加入取消列表，防止重新加入
        cancelingTimers_.insert(timer);     // reset() 函数会检查这个名单，如果发现在名单里，就不 insert，直接 delete。
    }
    assert(timers_.size() == activeTimers_.size());
}

// 当 timerfd 到期，Channel 触发读事件，EventLoop 回调此函数。
void TimerQueue::handleRead()
{
    loop_->assertInLoopThread();
    Timestamp now(Timestamp::now());
    readTimerfd(timerfd_, now);

    //获取所有已到期的定时器 这不仅仅是拿一个，而是拿“此刻之前所有”未执行的任务（防止回调堆积导致某些任务被跳过）
    std::vector<Entry> expired = getExpired(now);

    callingExpiredTimers_ = true;       // 标记进入“回调执行状态”
    cancelingTimers_.clear();           // 清空“正在取消”的辅助列表（上一轮的垃圾清理）
    // 调用到期定时器的回调函数
    for (const Entry& it : expired)
    {
        it.second->run();
    }
    callingExpiredTimers_ = false;      // 结束“回调执行状态”

    reset(expired, now);        //【核心】重置重复定时器
}

std::vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now)
{
    assert(timers_.size() == activeTimers_.size());
    std::vector<Entry> expired;

    // 找到“大于 now”的第一个元素  找到所有 <= now 的区间的结束位置。
    Entry sentry(now, reinterpret_cast<Timer*>(UINTPTR_MAX));

    // 2. 二分查找   lower_bound 返回第一个 >= sentry 的元素。 由于 sentry 是 (now, MAX)，所以返回的是第一个“时间 > now”的元素。
    // [begin, end) 区间内的所有元素，时间都 <= now。
    TimerList::iterator end = timers_.lower_bound(sentry);


    assert(end == timers_.end() || now < end->first);

    // 将 [begin, end) 的所有任务移动到 expired 向量中
    std::copy(timers_.begin(), end, back_inserter(expired));
    timers_.erase(timers_.begin(), end);        // 从 timers_ 中移除

    // 从 activeTimers_ 中移除
    for (const Entry& it : expired)
    {
        ActiveTimer timer(it.second, it.second->sequence());
        size_t n = activeTimers_.erase(timer);
        assert(n == 1); (void)n;
    }

    assert(timers_.size() == activeTimers_.size());
    return expired;
}

//处理完一波任务后，有的任务要销毁，有的要循环，最后还要重新定闹钟。
void TimerQueue::reset(const std::vector<Entry>& expired, Timestamp now)
{
    Timestamp nextExpire;

    for (const Entry& it : expired)
    {
        ActiveTimer timer(it.second, it.second->sequence());
        
        // 1. 判断是否重复 (repeat) 且 未被取消 (cancelingTimers_)
        // 场景：如果是一个重复定时器，但在它自己的回调函数里调用了 cancel()，
        // 那么它会出现在 cancelingTimers_ 列表中。此时虽然它 repeat 为 true，也不应该重启。
        if (it.second->repeat()  && cancelingTimers_.find(timer) == cancelingTimers_.end())
        {
            it.second->restart(now);        // 计算新时间：now + interval
            insert(it.second);                      // 重新塞回 timers_ 和 activeTimers_
        }
        else
        {
            // 彻底销毁  一次性定时器，或者已被取消的重复定时器，在此处释放内存
            delete it.second;
        }
    }

    // 重置 timerfd 为下一个最早到期的定时器
    if (!timers_.empty())
    {
        nextExpire = timers_.begin()->second->expiration();
    }

    if (nextExpire.valid())
    {
        resetTimerfd(timerfd_, nextExpire);     // 重置定时器的超时时间
    }
}

bool TimerQueue::insert(Timer* timer)
{
    loop_->assertInLoopThread();
    assert(timers_.size() == activeTimers_.size());
    bool earliestChanged = false;
    Timestamp when = timer->expiration();
    TimerList::iterator it = timers_.begin();
    // 如果列表为空 || 新定时器的到期时间比当前最早的还早
    if (it == timers_.end() || when < it->first)
    {
        earliestChanged = true;
    }
    {   // 1. 插入到按时间排序的 timers_ 中
        std::pair<TimerList::iterator, bool> result
            = timers_.insert(Entry(when, timer));
        assert(result.second); (void)result;
    }
    {       // 2. 插入到按地址排序的 activeTimers_ 中（为了 cancel）
        std::pair<ActiveTimerSet::iterator, bool> result
            = activeTimers_.insert(ActiveTimer(timer, timer->sequence()));
        assert(result.second); (void)result;
    }

    assert(timers_.size() == activeTimers_.size());
    return earliestChanged;
}
}