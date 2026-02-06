#ifndef LSK_MUDUO_NET_TIMERID_H
#define LSK_MUDUO_NET_TIMERID_H

#include <stdint.h>

namespace lsk_muduo {

class Timer;

class TimerId {
public:
    TimerId()
        : timer_(nullptr), sequence_(0)
    {
    }

    TimerId(Timer *timer, int64_t seq)
        : timer_(timer), sequence_(seq)
    {
    }

    friend class TimerQueue;

private:
    Timer *timer_;
    int64_t sequence_;
};

}  // namespace lsk_muduo

#endif  // LSK_MUDUO_NET_TIMERID_H