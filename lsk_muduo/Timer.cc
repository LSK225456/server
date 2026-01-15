#include "Timer.h"

std::atomic<int64_t> Timer::s_numCreated_(0);

void Timer::restart(Timestamp now)
{
    if (repeat_)
    {
        expiration_ = addTime(now, interval_);
    }
    else
    {
        expiration_ = Timestamp::invalid();
    }
}