#include "Timer.h"
namespace lsk_muduo {

std::atomic<int64_t> Timer::s_numCreated_(0);

void Timer::restart(Timestamp now)
{
    if (repeat_)
    {   
        expiration_ = addTime(now, interval_);      // 如果是重复定时器，下一次超时时间 = 当前时间 + 间隔时间
    }
    else
    {
        expiration_ = Timestamp::invalid();     // 如果不是重复定时器，设为无效时间，后续会被清理
    }
}
}