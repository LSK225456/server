#include "Poller.h"
#include "EPollPoller.h"

#include <stdlib.h>
namespace lsk_muduo {
Poller* Poller::newDefaultPoller(EventLoop *loop)
{
    if(::getenv("MUDUO_USE_POLL"))
    {
        return nullptr;
    }
    else
    {
        return new EPollPoller(loop);
    }
}
}
