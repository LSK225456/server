#pragma once

#include "noncopyable.h"
#include "Timestamp.h"
#include "Callbacks.h"
#include <atomic>

// 定时器类，封装定时任务的所有信息
class Timer : noncopyable
{
public:
    Timer(TimerCallback cb, Timestamp when, double interval)
        : callback_(std::move(cb)),
          expiration_(when),
          interval_(interval),
          repeat_(interval > 0.0),
          sequence_(s_numCreated_++)
    {
    }

    void run() const
    {
        callback_();
    }

    Timestamp expiration() const { return expiration_; }
    bool repeat() const { return repeat_; }
    int64_t sequence() const { return sequence_; }

    void restart(Timestamp now);

    static int64_t numCreated() { return s_numCreated_; }

private:
    const TimerCallback callback_;       // 定时器回调函数
    Timestamp expiration_;               // 到期时间
    const double interval_;              // 重复间隔（秒）
    const bool repeat_;                  // 是否重复
    const int64_t sequence_;             // 定时器序号

    static std::atomic<int64_t> s_numCreated_;  // 定时器计数，用于生成唯一序号
};