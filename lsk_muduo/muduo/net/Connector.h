#pragma once

#include "../base/noncopyable.h"
#include "InetAddress.h"

#include <functional>
#include <memory>
#include <atomic>
namespace lsk_muduo {
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

    void setNewConnectionCallback(const NewConnectionCallback& cb)  // 设置回调，当 socket 连接建立成功时调用
    { newConnectionCallback_ = cb; }

    void start();     // 可以在任意线程调用
    void restart();   // 必须在 loop 线程调用
    void stop();      // 可以在任意线程调用

    const InetAddress& serverAddress() const { return serverAddr_; }

private:
    // kConnecting: 正在连接中（调用了 ::connect 但还没返回成功，正在 epoll_wait 写事件
    // kConnected: 连接已建立（一旦建立，Connector 的任务就完成了，控制权移交给 TcpConnection）
    enum States { kDisconnected, kConnecting, kConnected };
    static const int kMaxRetryDelayMs = 30 * 1000;  // 最大重试延迟 30秒
    static const int kInitRetryDelayMs = 500;       // 初始重试延迟 500毫秒

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
    InetAddress serverAddr_;        // 目标服务器地址
    std::atomic_bool connect_;  // 是否处于连接状态
    States state_;              // 连接状态
    std::unique_ptr<Channel> channel_;      // Connector 持有的 Channel，只关注 write 事件
    NewConnectionCallback newConnectionCallback_;
    int retryDelayMs_;         // 当前的重连延迟时间（毫秒）
};
}