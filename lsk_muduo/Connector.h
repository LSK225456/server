#pragma once

#include "noncopyable.h"
#include "InetAddress.h"

#include <functional>
#include <memory>
#include <atomic>

class Channel;
class EventLoop;
class TimerId; 

// Connector 负责主动发起连接，处理非阻塞 connect
class Connector : noncopyable, public std::enable_shared_from_this<Connector>
{
public:
    using NewConnectionCallback = std::function<void(int sockfd)>;

    Connector(EventLoop* loop, const InetAddress& serverAddr);
    ~Connector();

    void setNewConnectionCallback(const NewConnectionCallback& cb)
    { newConnectionCallback_ = cb; }

    void start();     // 可以在任意线程调用
    void restart();   // 必须在 loop 线程调用
    void stop();      // 可以在任意线程调用

    const InetAddress& serverAddress() const { return serverAddr_; }

private:
    enum States { kDisconnected, kConnecting, kConnected };
    static const int kMaxRetryDelayMs = 30 * 1000;  // 30秒
    static const int kInitRetryDelayMs = 500;       // 500毫秒

    void setState(States s) { state_ = s; }
    void startInLoop();
    void stopInLoop();
    void connect();
    void connecting(int sockfd);
    void handleWrite();
    void handleError();
    void retry(int sockfd);
    int removeAndResetChannel();
    void resetChannel();

    EventLoop* loop_;
    InetAddress serverAddr_;
    std::atomic_bool connect_;  // 是否处于连接状态
    States state_;              // 连接状态
    std::unique_ptr<Channel> channel_;
    NewConnectionCallback newConnectionCallback_;
    int retryDelayMs_;          // 重连延迟时间（毫秒）
};