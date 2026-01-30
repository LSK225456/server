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
        LOG_FATAL << "sockets::createNonblockingSocket";
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

// 这是一个网络编程中的经典坑。
// 现象：当客户端尝试连接本机端口，且该端口范围正好覆盖了 ephemeral port（临时端口）范围时，
// 操作系统可能会分配一个源端口，正好等于目的端口，导致“自己连上了自己”。
// 结果：连接建立成功，但发数据就是发给自己。这在 TcpClient 重连逻辑中必须处理。
static bool isSelfConnect(int sockfd)
{
    // ... 获取本地地址 ...
    struct sockaddr_in localaddr;
    bzero(&localaddr, sizeof localaddr);
    socklen_t addrlen = sizeof localaddr;
    if (::getsockname(sockfd, (struct sockaddr*)(&localaddr), &addrlen) < 0)
    {
        LOG_ERROR << "sockets::getsockname";
        return false;
    }

    // ... 获取对端地址 ...
    struct sockaddr_in peeraddr;
    bzero(&peeraddr, sizeof peeraddr);
    addrlen = sizeof peeraddr;
    if (::getpeername(sockfd, (struct sockaddr*)(&peeraddr), &addrlen) < 0)
    {
        LOG_ERROR << "sockets::getpeername";
        return false;
    }
    // 核心判断：源IP==目的IP 且 源端口==目的端口
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
    LOG_DEBUG << "Connector::Connector[" << this << "]";
}

Connector::~Connector()
{
    LOG_DEBUG << "Connector::~Connector[" << this << "]";
    assert(!channel_);
}

void Connector::start()         // 用户接口
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
        connect();      // 发起系统调用
    }
    else
    {
        LOG_DEBUG << "do not connect";
    }
}

void Connector::stop()      // 用户接口
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
        int sockfd = removeAndResetChannel();       // 移除 Channel，不再关注 socket 事件
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
            LOG_ERROR << "connect error in Connector::startInLoop " << savedErrno;
            ::close(sockfd);
            break;

        default:
            LOG_ERROR << "Unexpected error in Connector::startInLoop " << savedErrno;
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

//当 connect 返回 EINPROGRESS 时，让 epoll 帮我们盯着“什么时候连上”
void Connector::connecting(int sockfd)
{
    setState(kConnecting);
    assert(!channel_);
    channel_.reset(new Channel(loop_, sockfd));     // 创建 Channel 绑定 sockfd
    
    // 只关心写事件
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

// 当 epoll 通知 socket 可写时，可能有三种情况：连接成功。连接失败（例如被拒绝），socket 也是可写的。自连接
void Connector::handleWrite()
{
    LOG_INFO << "Connector::handleWrite state=" << static_cast<int>(state_);

    if (state_ == kConnecting)
    {
        // 1. 移除 Channel   连接结果已出，不再需要监听 connect 过程了。
        // 注意：removeAndResetChannel 会把 socket 从 epoll 中摘除。
        int sockfd = removeAndResetChannel();
        int err = getSocketError(sockfd);
        if (err)    // 情况 A: 有错误
        {
            LOG_ERROR << "Connector::handleWrite - SO_ERROR = " << err;
            retry(sockfd);  // 失败重连
        }
        else if (isSelfConnect(sockfd))     // 情况 B: 自连接
        {
            LOG_ERROR << "Connector::handleWrite - Self connect";  
            retry(sockfd);      
        }
        else        // 情况 C: 真正的成功
        {
            setState(kConnected);
            if (connect_)       // 再次确认用户意图（防止中途被 stop）
            {
                // 调用 TcpClient::newConnection(sockfd)。此时，Connector 把 sockfd 的控制权移交出去了。
                newConnectionCallback_(sockfd);
            }
            else
            {
                ::close(sockfd);        // 用户已经不想连了，关掉
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
    LOG_ERROR << "Connector::handleError state=" << static_cast<int>(state_);
    if (state_ == kConnecting)
    {
        int sockfd = removeAndResetChannel();
        int err = getSocketError(sockfd);
        LOG_ERROR << "SO_ERROR = " << err;
        retry(sockfd);
    }
}

void Connector::retry(int sockfd)
{
    // 1. 关闭旧的 socket  不能复用连接失败的 socket。
    ::close(sockfd);
    setState(kDisconnected);
    if (connect_)       // 如果用户还想连（没有调用 stop）
    {
        LOG_INFO << "Connector::retry - Retry connecting to " << serverAddr_.toIpPort() << " in " << retryDelayMs_ << " milliseconds.";

        // 使用定时器实现延迟重连    startInLoop 会再次调用 connect()。
        loop_->runAfter(retryDelayMs_ / 1000.0,
                        std::bind(&Connector::startInLoop, shared_from_this()));
        
        // 指数退避，但不超过最大延迟   下一次延迟加倍：0.5s -> 1s -> 2s ... 直到 30s
        retryDelayMs_ = std::min(retryDelayMs_ * 2, kMaxRetryDelayMs);
    }
    else
    {
        LOG_DEBUG << "do not connect";
    }
}