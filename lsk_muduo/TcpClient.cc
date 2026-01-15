#include "TcpClient.h"
#include "Logger.h"
#include "Connector.h"
#include "EventLoop.h"
#include "Socket.h"
#include "TimerId.h" 

#include <stdio.h>
#include <sys/socket.h>
#include <strings.h>
#include <cassert>  

// 获取对端地址
static InetAddress getPeerAddr(int sockfd)
{
    struct sockaddr_in addr;
    bzero(&addr, sizeof addr);
    socklen_t addrlen = sizeof addr;
    if (::getpeername(sockfd, (struct sockaddr*)(&addr), &addrlen) < 0)
    {
        LOG_ERROR("sockets::getPeerAddr");
    }
    return InetAddress(addr);
}

// 获取本地地址
static InetAddress getLocalAddr(int sockfd)
{
    struct sockaddr_in addr;
    bzero(&addr, sizeof addr);
    socklen_t addrlen = sizeof addr;
    if (::getsockname(sockfd, (struct sockaddr*)(&addr), &addrlen) < 0)
    {
        LOG_ERROR("sockets::getLocalAddr");
    }
    return InetAddress(addr);
}

// 默认的连接回调
static void defaultConnectionCallback(const TcpConnectionPtr& conn)
{
    LOG_INFO("TcpClient - connection %s -> %s is %s",
             conn->localAddress().toIpPort().c_str(),
             conn->peerAddress().toIpPort().c_str(),
             conn->connected() ? "UP" : "DOWN");
}

// 默认的消息回调
static void defaultMessageCallback(const TcpConnectionPtr& conn,
                                    Buffer* buffer,
                                    Timestamp receiveTime)
{
    buffer->retrieveAll();
}

TcpClient::TcpClient(EventLoop* loop,
                     const InetAddress& serverAddr,
                     const std::string& nameArg)
    : loop_(loop),
      connector_(new Connector(loop, serverAddr)),      // 创建 Connector  持有 serverAddr
      name_(nameArg),
      connectionCallback_(defaultConnectionCallback),       // 设置默认回调，防止空调用
      messageCallback_(defaultMessageCallback),
      retry_(false),
      connect_(true),
      nextConnId_(1)
{
    // 告诉 Connector：“如果你连上了，就把 sockfd 传给我的 newConnection 函数”
    connector_->setNewConnectionCallback(
        std::bind(&TcpClient::newConnection, this, std::placeholders::_1));
    LOG_INFO("TcpClient::TcpClient[%s] - connector %p", name_.c_str(), connector_.get());
}

TcpClient::~TcpClient()
{
    LOG_INFO("TcpClient::~TcpClient[%s] - connector %p", name_.c_str(), connector_.get());
    TcpConnectionPtr conn;
    bool unique = false;
    {
        // 检查当前是否持有连接，且是否是唯一持有者
        std::lock_guard<std::mutex> lock(mutex_);
        unique = connection_.unique();
        conn = connection_;
    }
    if (conn)
    {
        assert(loop_ == conn->getLoop());
        // 设置关闭回调，在连接关闭时清理资源
        CloseCallback cb = std::bind(&TcpClient::removeConnection, this, std::placeholders::_1);
        loop_->runInLoop(
            std::bind(&TcpConnection::setCloseCallback, conn, cb));
        if (unique)
        {
            conn->shutdown();
        }
    }
    else
    {
        connector_->stop();
    }
}

void TcpClient::connect()
{
    LOG_INFO("TcpClient::connect[%s] - connecting to %s",
             name_.c_str(), connector_->serverAddress().toIpPort().c_str());
    connect_ = true;
    connector_->start();
}

void TcpClient::disconnect()
{
    connect_ = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (connection_)
        {
            connection_->shutdown();
        }
    }
}

void TcpClient::stop()
{
    connect_ = false;
    connector_->stop();
}

void TcpClient::newConnection(int sockfd)
{
    loop_->assertInLoopThread();
    InetAddress peerAddr = getPeerAddr(sockfd);
    char buf[32];
    snprintf(buf, sizeof buf, ":%s#%d", peerAddr.toIpPort().c_str(), nextConnId_);
    ++nextConnId_;
    std::string connName = name_ + buf;

    InetAddress localAddr = getLocalAddr(sockfd);
    
    // 创建 TcpConnection
    TcpConnectionPtr conn(new TcpConnection(loop_,
                                            connName,
                                            sockfd,
                                            localAddr,
                                            peerAddr));

    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);
    conn->setCloseCallback(
        std::bind(&TcpClient::removeConnection, this, std::placeholders::_1));
    {
        std::lock_guard<std::mutex> lock(mutex_);
        connection_ = conn;
    }
    conn->connectEstablished();
}

void TcpClient::removeConnection(const TcpConnectionPtr& conn)
{
    loop_->assertInLoopThread();
    assert(loop_ == conn->getLoop());

    {
        std::lock_guard<std::mutex> lock(mutex_);
        assert(connection_ == conn);
        connection_.reset();
    }

    loop_->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
    if (retry_ && connect_)
    {
        LOG_INFO("TcpClient::connect[%s] - Reconnecting to %s",
                 name_.c_str(), connector_->serverAddress().toIpPort().c_str());
        connector_->restart();
    }
}