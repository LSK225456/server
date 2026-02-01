#include "Acceptor.h"
#include "../base/Logger.h"
#include "InetAddress.h"

#include <sys/types.h>    
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>

static int createNonblocking()
{
    int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (sockfd < 0)
    {
        LOG_FATAL << __FILE__ << ":" << __FUNCTION__ << ":" << __LINE__ << " listen socket create err:" << errno;
    }
    return sockfd;
}


Acceptor::Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport)
    : loop_(loop)
    , acceptSocket_(createNonblocking())
    , acceptChannel_(loop, acceptSocket_.fd())
    , listenning_(false)
{
    acceptSocket_.setReuseAddr(true);
    acceptSocket_.setReusePort(true);
    acceptSocket_.bindAddress(listenAddr);
    
    acceptChannel_.setReadCallback(std::bind(&Acceptor::handleRead, this));
}


Acceptor::~Acceptor()
{
    acceptChannel_.disableAll();
    acceptChannel_.remove();
}


void Acceptor::listen()
{
    listenning_ = true;
    acceptSocket_.listen();
    acceptChannel_.enableReading();
}


void Acceptor::handleRead()
{
    InetAddress peerAddr;
    int connfd = acceptSocket_.accept(&peerAddr);
    if (connfd >= 0)
    {
        if (newConnectionCallback_)
        {
            newConnectionCallback_(connfd, peerAddr);
        }
        else 
        {
            ::close(connfd);
        }
    }
    else
    {
       LOG_ERROR << __FILE__ << ":" << __FUNCTION__ << ":" << __LINE__ << " accept err:" << errno;
        if (errno == EMFILE)    // 系统分配的文件描述符fd上限达到，应该提高分配上限，做分布式处理
        {
            LOG_ERROR << __FILE__ << ":" << __FUNCTION__ << ":" << __LINE__ << " sockfd reached limit!";
        }
    }
}