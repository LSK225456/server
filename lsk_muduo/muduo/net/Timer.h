#pragma once

#include "../base/noncopyable.h"
#include "../base/Timestamp.h"
#include "Callbacks.h"
#include <atomic>
namespace lsk_muduo {

// 定时器类，封装定时任务的所有信息
class Timer : noncopyable
{
public:
    Timer(TimerCallback cb, Timestamp when, double interval)
        : callback_(std::move(cb)),
          expiration_(when),            // 初始化到期时间
          interval_(interval),              // 初始化间隔
          repeat_(interval > 0.0),      // 如果 interval > 0，则标记为重复定时器
          sequence_(s_numCreated_++)        // 获取当前全局计数并自增，赋予该 Timer 唯一的序列号
    {
    }

    void run() const
    {
        callback_();
    }

    Timestamp expiration() const { return expiration_; }
    bool repeat() const { return repeat_; }
    int64_t sequence() const { return sequence_; }

    void restart(Timestamp now);        // 重新计算下一次超时时间（仅用于重复定时器）

    static int64_t numCreated() { return s_numCreated_; }

private:
    const TimerCallback callback_;       // 定时器回调函数
    Timestamp expiration_;               // 到期时间
    const double interval_;              // 重复间隔（秒）
    const bool repeat_;                  // 是否重复
    const int64_t sequence_;             // 定时器序号

    static std::atomic<int64_t> s_numCreated_;  // 定时器计数，用于生成唯一序号
};
}