#include "muduo/net/TcpServer.h"
#include "muduo/net/EventLoop.h"
#include <iostream>
#include <functional>
#include <string>

using namespace std;
using namespace std::placeholders;
/* 
基于muduo网络库开发服务器程序 
1.组合TcpServer对象 
2.创建EventLoop事件循环对象的指针
3.明确TcpServer构造函数需要什么参数，输出ChatServer的构造函数
4.在当前服务器类的构造函数中，注册处理连接的回调函数和处理读写事件的回调函数
5.设置合适的服务端线程数量
*/
class ChatServer
{
public:
    ChatServer(EventLoop* loop,  //事件循环
            const InetAddress& listenAddr, //IP + 端口
            const string& nameArg) //服务器的名称
        : _server(loop, listenAddr, nameArg), _loop(loop)       // 对 _server 成员调用其构造函数
    {
        // 告诉 muduo以后如果有客户端 connect 进来，或者断开连接，请调用我的 onConnection 函数
        _server.setConnectionCallback(bind(&ChatServer::onConnection, this, _1)); //参数占位符

        // 告诉 muduo如果已连接的 socket 上有数据可读（POLLIN 事件），请调用我的 onMessage 函数”
        _server.setMessageCallback(bind(&ChatServer::onMessage, this, _1, _2, _3));

        // 设置线程池。muduo 采用 One Loop Per Thread 模型。

        // 运行方式：主线程 (loop) 只负责 accept 新连接。新连接建立后，会从线程池中拿出
        // 一个工作线程，把这个连接的后续读写操作全部交给那个线程处理。这比单线程 epoll 效率高得多。
        _server.setThreadNum(4); 
    }

    void start()
    {
        _server.start();        // 底层执行：socket() -> bind() -> listen()。
    }

private:
    TcpServer _server;
    EventLoop *_loop;

    //专门处理用户的连接创建和断开  epoll  listenfd  accept
    void onConnection(const TcpConnectionPtr& conn)
    {
        if (conn->connected())
        {
            cout << conn->peerAddress().toIpPort() << "->" << 
                conn->localAddress().toIpPort() << "state:online" <<endl;
        }
        else 
        {
            cout << conn->peerAddress().toIpPort() << "->" << 
            conn->localAddress().toIpPort() << "state:offline" <<endl;
            conn->shutdown(); // close(fd)
            // _loop->quit();
        }
    }

    //专门处理用户的读写事件
    void onMessage(const TcpConnectionPtr& conn, //连接
        Buffer* buff, //缓冲区
         Timestamp time) //接受到数据的时间信息
    {
        string buf = buff->retrieveAllAsString();
        cout << "recv data :" << buf << "time:" << time.toString() <<endl; 
        conn->send(buf);
    }
};

int main()
{
    // 相当于创建了一个 epoll 实例（epoll_create）。它是一个反应堆（Reactor），负责监控文件描述符的事件 。
    EventLoop loop;     

    // 封装了 IP 和 Port，相当于准备好了 struct sockaddr_in
    InetAddress addr(6000, "127.0.0.1"); 

    // 定义的类，它初始化了内部的 TcpServer 对象。
    ChatServer server(&loop, addr, "ChatServer");
    server.start(); // listenfd epoll_ctl => epoll
    loop.loop();    //  底层执行：进入 while(quit) 循环，调用 epoll_wait
    return 0;
}

