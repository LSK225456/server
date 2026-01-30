#pragma once

#include "noncopyable.h"
#include "InetAddress.h"
#include "Callbacks.h"
#include "Buffer.h"
#include "Timestamp.h"

#include <memory>
#include <string>
#include <atomic>

class Channel;
class EventLoop;
class Socket;

class TcpConnection : noncopyable, public std::enable_shared_from_this<TcpConnection>
{
public:
    TcpConnection(EventLoop *loop,
                    const std::string &name,
                    int sockfd,
                    const InetAddress& localAddr,
                    const InetAddress& peerAddr);
                
    ~TcpConnection();

    EventLoop* getLoop() const { return loop_;}
    const std::string& name() const { return name_; }
    const InetAddress& localAddress() const { return localAddr_; }
    const InetAddress& peerAddress() const { return peerAddr_; } 

    bool connected() const { return state_ == kConnected; }

    void send(const std::string &buf);

    // 优雅关闭：先关闭写端，等待数据发送完毕
    void shutdown();
    
    // ============ 强制关闭接口 ============
    /**
     * forceClose - 强制立即关闭连接
     * 
     * 与 shutdown() 的区别：
     * - shutdown(): 优雅关闭，只关闭写端(半关闭)，等待发送缓冲区数据发完
     * - forceClose(): 直接关闭整个连接，丢弃未发送的数据
     * 
     * 使用场景：
     * - 检测到恶意客户端
     * - 连接超时需要立即踢掉
     * - 服务器关闭时强制断开所有连接
     * 
     * 线程安全：可在任意线程调用
     */
    void forceClose();
    
    /**
     * forceCloseWithDelay - 延迟强制关闭（超时踢人）
     * 
     * 典型用途：设置一个超时时间，如果在此期间连接未正常关闭，则强制关闭
     * 
     * 示例：shutdown() 后设置 3 秒超时
     *   conn->shutdown();
     *   conn->forceCloseWithDelay(3.0);  // 3秒后如果还没关闭则强制关闭
     * 
     * 注意：使用 WeakCallback 防止回调时 TcpConnection 已析构
     * 
     * @param seconds 延迟时间（秒）
     */
    void forceCloseWithDelay(double seconds);

    void setConnectionCallback(const ConnectionCallback& cb)
    { connectionCallback_ = cb; }

    void setMessageCallback(const MessageCallback& cb)
    { messageCallback_ = cb; }

    void setWriteCompleteCallback(const WriteCompleteCallback& cb)
    { writeCompleteCallback_ = cb; }

    void setHighWaterMarkCallback(const HighWaterMarkCallback& cb, size_t highWaterMark)
    { highWaterMarkCallback_ = cb; highWaterMark_ = highWaterMark; }

    void setCloseCallback(const CloseCallback& cb)
    { closeCallback_ = cb; }

    void connectEstablished();

    void connectDestroyed();

private:
    enum StateE {kDisconnected, kConnecting, kConnected, kDisconnecting};
    void setState(StateE state) {state_ = state;}

    void handleRead(Timestamp receiveTime);
    void handleWrite();
    void handleClose();
    void handleError();

    void sendInLoop(const void* message, size_t len);
    void shutdownInLoop();
    void forceCloseInLoop();    // forceClose 的 loop 线程执行版本

    EventLoop *loop_;
    const std::string name_;
    std::atomic_int state_;
    bool reading_;

    std::unique_ptr<Socket> socket_;
    std::unique_ptr<Channel> channel_;

    const InetAddress localAddr_;
    const InetAddress peerAddr_;

    ConnectionCallback connectionCallback_; // 有新连接时的回调
    MessageCallback messageCallback_; // 有读写消息时的回调
    WriteCompleteCallback writeCompleteCallback_; // 消息发送完成以后的回调
    HighWaterMarkCallback highWaterMarkCallback_;
    CloseCallback closeCallback_;
    size_t highWaterMark_;

    Buffer inputBuffer_;
    Buffer outputBuffer_;
};