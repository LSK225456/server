#pragma once

#include "noncopyable.h"
#include "TcpConnection.h"
#include "InetAddress.h" 

#include <memory>
#include <string>
#include <mutex>
#include <atomic>

class Connector;
class EventLoop;

using ConnectorPtr = std::shared_ptr<Connector>;

// TcpClient 用于客户端发起连接
class TcpClient : noncopyable
{
public:
    TcpClient(EventLoop* loop,
              const InetAddress& serverAddr,
              const std::string& nameArg);
    ~TcpClient();

    void connect();
    void disconnect();
    void stop();

    TcpConnectionPtr connection() const     // 获取当前的 TcpConnection 对象
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return connection_;
    }

    EventLoop* getLoop() const { return loop_; }
    bool retry() const { return retry_; }
    void enableRetry() { retry_ = true; }

    const std::string& name() const { return name_; }

    // 用户通过这些接口设置业务逻辑，TcpClient 会把它们“转交”给新建的 TcpConnection
    void setConnectionCallback(ConnectionCallback cb)
    { connectionCallback_ = std::move(cb); }

    void setMessageCallback(MessageCallback cb)
    { messageCallback_ = std::move(cb); }

    void setWriteCompleteCallback(WriteCompleteCallback cb)
    { writeCompleteCallback_ = std::move(cb); }

private:
    // 当 Connector 成功连上 socket 后，会调用此函数   这是 Connector -> TcpClient 的交接点
    void newConnection(int sockfd);
    
    // 当 TcpConnection 断开时（收到 CloseCallback），会调用此函数  这是 TcpConnection -> TcpClient 的通知点，用于触发重连
    void removeConnection(const TcpConnectionPtr& conn);

    EventLoop* loop_;
    ConnectorPtr connector_;
    const std::string name_;
    ConnectionCallback connectionCallback_;
    MessageCallback messageCallback_;
    WriteCompleteCallback writeCompleteCallback_;
    std::atomic_bool retry_;    // 是否重连
    std::atomic_bool connect_;  // 是否连接
    int nextConnId_;            // 连接 ID
    mutable std::mutex mutex_;
    TcpConnectionPtr connection_;  // 当前连接
};