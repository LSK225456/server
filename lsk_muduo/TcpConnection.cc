#include "TcpConnection.h"
#include "Logger.h"
#include "Socket.h"
#include "Channel.h"
#include "EventLoop.h"
#include "WeakCallback.h"
#include "TimerId.h"

#include <functional>
#include <errno.h>
#include <sys/types.h>         
#include <sys/socket.h>
#include <strings.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <string>

static EventLoop* CheckLoopNotNull(EventLoop *loop)
{
    if (loop == nullptr)
    {
        LOG_FATAL << __FILE__ << ":" << __FUNCTION__ << ":" << __LINE__ << " TcpConnection Loop is null!";
    }
    return loop;
}


TcpConnection::TcpConnection(EventLoop *loop,
                const std::string &nameArg,
                int sockfd,
                const InetAddress& localAddr,
                const InetAddress& peerAddr)
            : loop_(CheckLoopNotNull(loop))
            , name_(nameArg)
            , state_(kConnecting)
            , reading_(true)
            , socket_(new Socket(sockfd))
            , channel_(new Channel(loop, sockfd))
            , localAddr_(localAddr)
            , peerAddr_(peerAddr)
            , highWaterMark_(64*1024*1024)
{
    channel_->setReadCallback(
        std::bind(&TcpConnection::handleRead, this, std::placeholders::_1)
    );
    channel_->setWriteCallback(
        std::bind(&TcpConnection::handleWrite, this)
    );
    channel_->setCloseCallback(
        std::bind(&TcpConnection::handleClose, this)
    );
    channel_->setErrorCallback(
        std::bind(&TcpConnection::handleError, this)
    );

    LOG_INFO << "TcpConnection::ctor[" << name_ << "] at fd=" << sockfd;
    socket_->setKeepAlive(true);
}
            

TcpConnection::~TcpConnection()
{
    LOG_INFO << "TcpConnection::dtor[" << name_ << "] at fd=" << channel_->fd() << " state=" << static_cast<int>(state_);    
}


void TcpConnection::send(const std::string &buf)
{
    if (state_ == kConnected)
    {
        if (loop_->isInLoopThread())
        {
            sendInLoop(buf.c_str(), buf.size());
        }
        else
        {
            loop_->runInLoop(std::bind(&TcpConnection::sendInLoop, 
                    this, buf.c_str(), buf.size()));
        }
    }
}


void TcpConnection::sendInLoop(const void* data, size_t len)
{
    ssize_t nwrote = 0;
    size_t remaining = len;
    bool faultError = false;

    if (state_ == kDisconnected)
    {
        LOG_ERROR << "disconnected, give up writing!";
        return;
    }

    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
    {
        nwrote = ::write(channel_->fd(), data, len);
        if (nwrote > 0)
        {
            remaining = len - nwrote;
            if (remaining == 0 && writeCompleteCallback_)
            {
                loop_->queueInLoop(
                    std::bind(writeCompleteCallback_, shared_from_this())
                );
            }
        }
        else
        {
            nwrote = 0;
            if (errno != EWOULDBLOCK)
            {
                LOG_ERROR << "TcpConnection::sendInLoop";
                if (errno == EPIPE || errno == ECONNRESET) // SIGPIPE  RESET
                {
                    faultError = true;
                }
            }
        }
    }

    if (!faultError && remaining > 0)
    {
        size_t oldLen = outputBuffer_.readableBytes();
        if (oldLen + remaining >= highWaterMark_
        && oldLen < highWaterMark_
        && highWaterMarkCallback_)
        {
            loop_->queueInLoop(std::bind(highWaterMarkCallback_, shared_from_this(), oldLen+remaining));
        }
        outputBuffer_.append((char*)data + nwrote, remaining);
        if (!channel_->isWriting())
        {
            channel_->enableWriting();
        }
    }
}


void TcpConnection::shutdown()
{
    if (state_ == kConnected)
    {
        setState(kDisconnecting);
        loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, this));
    }
}

void TcpConnection::shutdownInLoop()
{
    if (!channel_->isWriting())
    {
        socket_->shutdownWrite();
    }
}


void TcpConnection::forceClose()
{
    // 只有在 kConnected 或 kDisconnecting 状态才需要处理
    // kDisconnecting: 可能 shutdown() 后对端没响应，需要强制关闭
    if (state_ == kConnected || state_ == kDisconnecting)
    {
        setState(kDisconnecting);
        // 使用 queueInLoop 而非 runInLoop，确保在当前回调完成后执行
        // 使用 shared_from_this() 延长生命周期，确保回调执行时对象存活
        loop_->queueInLoop(std::bind(&TcpConnection::forceCloseInLoop, shared_from_this()));
    }
}


void TcpConnection::forceCloseWithDelay(double seconds)
{
    if (state_ == kConnected || state_ == kDisconnecting)
    {
        setState(kDisconnecting);
        // 使用 makeWeakCallback 而非直接 bind
        // 原因：定时器触发时 TcpConnection 可能已析构
        // WeakCallback 会检查 weak_ptr，若对象已死则不调用
        // 
        // 注意：这里调用 forceClose 而非 forceCloseInLoop
        // 避免竞态条件：runAfter 返回前连接可能被其他线程关闭
        loop_->runAfter(
            seconds,
            makeWeakCallback(shared_from_this(), &TcpConnection::forceClose));
    }
}


void TcpConnection::forceCloseInLoop()
{
    loop_->assertInLoopThread();
    // 再次检查状态，因为在排队期间可能已经被关闭
    if (state_ == kConnected || state_ == kDisconnecting)
    {
        // 模拟收到 0 字节（对端关闭），触发 handleClose
        // 这样可以复用已有的关闭逻辑
        handleClose();
    }
}


void TcpConnection::connectEstablished()
{
    setState(kConnected);
    channel_->tie(shared_from_this());
    channel_->enableReading();

    connectionCallback_(shared_from_this());
}


void TcpConnection::connectDestroyed()
{
    if (state_ == kConnected)
    {
        setState(kDisconnected);
        channel_->disableAll();
        connectionCallback_(shared_from_this());
    }
    channel_->remove();
}


void TcpConnection::handleRead(Timestamp receiveTime)
{
    int saveErrno = 0;
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &saveErrno);
    if (n > 0)
    {
        messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
    }
    else if (n == 0)
    {
        handleClose();
    }
    else
    {
        errno = saveErrno;
        LOG_ERROR << "TcpConnection::handleRead";
        handleError();
    }
}


void TcpConnection::handleWrite()
{
    if (channel_->isWriting())
    {
        int savedErrno = 0;
        ssize_t n = outputBuffer_.writeFd(channel_->fd(), &savedErrno);
        if (n > 0)
        {
            outputBuffer_.retrieve(n);
            if (outputBuffer_.readableBytes() == 0)
            {
                channel_->disableWriting();
                if (writeCompleteCallback_)
                {
                    loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
                }
                if (state_ == kDisconnecting)
                {
                    shutdownInLoop();
                }
            }
        } 
        else
        {
            LOG_ERROR << "TcpConnection::handleWrite";
        }
    }
    else
    {
        LOG_ERROR << "TcpConnection fd=" << channel_->fd() << " is down, no more writing";
    }
}


void TcpConnection::handleClose()
{
    LOG_INFO << "TcpConnection::handleClose fd=" << channel_->fd() << " state=" << static_cast<int>(state_);
    setState(kDisconnected);
    channel_->disableAll();

    TcpConnectionPtr connPtr(shared_from_this());
    connectionCallback_(connPtr);
    closeCallback_(connPtr);
}


void TcpConnection::handleError()
{
    int optval;
    socklen_t optlen = sizeof optval;
    int err = 0;
    if(::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
    {
        err = errno;
    }
    else
    {
        err = optval;
    }
    LOG_ERROR << "TcpConnection::handleError name:" << name_ << " - SO_ERROR:" << err;
}






