#include "Connector.h"
#include "Logger.h"
#include "Channel.h"
#include "EventLoop.h"
#include "TimerId.h"

#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <strings.h>
#include <cassert>

// 创建非阻塞 socket
static int createNonblockingSocket()
{
    int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (sockfd < 0)
    {
        LOG_FATAL("sockets::createNonblockingSocket");
    }
    return sockfd;
}

// 获取 socket 错误
static int getSocketError(int sockfd)
{
    int optval;
    socklen_t optlen = sizeof optval;
    if (::getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
    {
        return errno;
    }
    else
    {
        return optval;
    }
}

// 检查是否自连接
static bool isSelfConnect(int sockfd)
{
    struct sockaddr_in localaddr;
    bzero(&localaddr, sizeof localaddr);
    socklen_t addrlen = sizeof localaddr;
    if (::getsockname(sockfd, (struct sockaddr*)(&localaddr), &addrlen) < 0)
    {
        LOG_ERROR("sockets::getsockname");
        return false;
    }

    struct sockaddr_in peeraddr;
    bzero(&peeraddr, sizeof peeraddr);
    addrlen = sizeof peeraddr;
    if (::getpeername(sockfd, (struct sockaddr*)(&peeraddr), &addrlen) < 0)
    {
        LOG_ERROR("sockets::getpeername");
        return false;
    }

    return localaddr.sin_port == peeraddr.sin_port
        && localaddr.sin_addr.s_addr == peeraddr.sin_addr.s_addr;
}

Connector::Connector(EventLoop* loop, const InetAddress& serverAddr)
    : loop_(loop),
      serverAddr_(serverAddr),
      connect_(false),
      state_(kDisconnected),
      retryDelayMs_(kInitRetryDelayMs)
{
    LOG_DEBUG("Connector::Connector[%p]", this);
}

Connector::~Connector()
{
    LOG_DEBUG("Connector::~Connector[%p]", this);
    assert(!channel_);
}

void Connector::start()
{
    connect_ = true;
    loop_->runInLoop(std::bind(&Connector::startInLoop, this));
}

void Connector::startInLoop()
{
    loop_->assertInLoopThread();
    assert(state_ == kDisconnected);
    if (connect_)
    {
        connect();
    }
    else
    {
        LOG_DEBUG("do not connect");
    }
}

void Connector::stop()
{
    connect_ = false;
    loop_->queueInLoop(std::bind(&Connector::stopInLoop, this));
}

void Connector::stopInLoop()
{
    loop_->assertInLoopThread();
    if (state_ == kConnecting)
    {
        setState(kDisconnected);
        int sockfd = removeAndResetChannel();
        retry(sockfd);
    }
}

void Connector::connect()
{
    int sockfd = createNonblockingSocket();
    int ret = ::connect(sockfd, (struct sockaddr*)(serverAddr_.getSockAddr()), 
                        sizeof(struct sockaddr_in));
    int savedErrno = (ret == 0) ? 0 : errno;
    
    switch (savedErrno)
    {
        case 0:
        case EINPROGRESS:  // 非阻塞 socket 正在连接
        case EINTR:        // 被信号中断
        case EISCONN:      // 已连接
            connecting(sockfd);
            break;

        case EAGAIN:
        case EADDRINUSE:
        case EADDRNOTAVAIL:
        case ECONNREFUSED:
        case ENETUNREACH:
            retry(sockfd);
            break;

        case EACCES:
        case EPERM:
        case EAFNOSUPPORT:
        case EALREADY:
        case EBADF:
        case EFAULT:
        case ENOTSOCK:
            LOG_ERROR("connect error in Connector::startInLoop %d", savedErrno);
            ::close(sockfd);
            break;

        default:
            LOG_ERROR("Unexpected error in Connector::startInLoop %d", savedErrno);
            ::close(sockfd);
            break;
    }
}

void Connector::restart()
{
    loop_->assertInLoopThread();
    setState(kDisconnected);
    retryDelayMs_ = kInitRetryDelayMs;
    connect_ = true;
    startInLoop();
}

void Connector::connecting(int sockfd)
{
    setState(kConnecting);
    assert(!channel_);
    channel_.reset(new Channel(loop_, sockfd));
    channel_->setWriteCallback(
        std::bind(&Connector::handleWrite, this));
    channel_->setErrorCallback(
        std::bind(&Connector::handleError, this));

    // 关注可写事件，连接成功后 socket 变为可写
    channel_->enableWriting();
}

int Connector::removeAndResetChannel()
{
    channel_->disableAll();
    channel_->remove();
    int sockfd = channel_->fd();
    // 不能在这里 reset channel_，因为我们在 Channel::handleEvent 中
    loop_->queueInLoop(std::bind(&Connector::resetChannel, this));
    return sockfd;
}

void Connector::resetChannel()
{
    channel_.reset();
}

void Connector::handleWrite()
{
    LOG_INFO("Connector::handleWrite state=%d", static_cast<int>(state_));

    if (state_ == kConnecting)
    {
        int sockfd = removeAndResetChannel();
        int err = getSocketError(sockfd);
        if (err)
        {
            LOG_ERROR("Connector::handleWrite - SO_ERROR = %d", err);
            retry(sockfd);
        }
        else if (isSelfConnect(sockfd))
        {
            LOG_ERROR("Connector::handleWrite - Self connect");  
            retry(sockfd);
        }
        else
        {
            setState(kConnected);
            if (connect_)
            {
                newConnectionCallback_(sockfd);
            }
            else
            {
                ::close(sockfd);
            }
        }
    }
    else
    {
        // 不应该到达这里
        assert(state_ == kDisconnected);
    }
}

void Connector::handleError()
{
    LOG_ERROR("Connector::handleError state=%d", static_cast<int>(state_));
    if (state_ == kConnecting)
    {
        int sockfd = removeAndResetChannel();
        int err = getSocketError(sockfd);
        LOG_ERROR("SO_ERROR = %d", err);
        retry(sockfd);
    }
}

void Connector::retry(int sockfd)
{
    ::close(sockfd);
    setState(kDisconnected);
    if (connect_)
    {
        LOG_INFO("Connector::retry - Retry connecting to %s in %d milliseconds.",
                 serverAddr_.toIpPort().c_str(), retryDelayMs_);
        // 使用定时器实现延迟重连
        loop_->runAfter(retryDelayMs_ / 1000.0,
                        std::bind(&Connector::startInLoop, shared_from_this()));
        // 指数退避，但不超过最大延迟
        retryDelayMs_ = std::min(retryDelayMs_ * 2, kMaxRetryDelayMs);
    }
    else
    {
        LOG_DEBUG("do not connect");
    }
}