#pragma once

class Timer;

// 对外的定时器标识符，用于取消定时器
class TimerId
{
public:
    TimerId()
        : timer_(nullptr),
          sequence_(0)
    {
    }

    TimerId(Timer* timer, int64_t seq)
        : timer_(timer),
          sequence_(seq)
    {
    }

    // 默认拷贝构造、析构和赋值都可以

    friend class TimerQueue;

private:
    Timer* timer_;
    int64_t sequence_;
};