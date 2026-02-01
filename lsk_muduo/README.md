# lsk_muduo - C++11 Muduo网络库

## 项目简介

lsk_muduo 是一个基于C++11标准实现的Muduo网络库，采用Reactor模式，支持高并发TCP网络编程。

## 项目结构

```
lsk_muduo/
├── muduo/                    # muduo核心库代码
│   ├── base/                 # 基础组件模块
│   │   ├── Thread.h/cc       # 线程封装
│   │   ├── ThreadPool.h/cc   # 线程池
│   │   ├── Timestamp.h/cc    # 时间戳
│   │   ├── Logger.h/cc       # 日志系统
│   │   ├── LogStream.h/cc    # 日志流
│   │   ├── LogFile.h/cc      # 日志文件
│   │   ├── AsyncLogging.h/cc # 异步日志
│   │   ├── CurrentThread.h/cc# 当前线程信息
│   │   └── noncopyable.h     # 不可拷贝基类
│   │
│   └── net/                  # 网络模块
│       ├── EventLoop.h/cc    # 事件循环
│       ├── Channel.h/cc      # 通道（封装fd和事件）
│       ├── Poller.h/cc       # IO多路复用基类
│       ├── EPollPoller.h/cc  # EPoll实现
│       ├── Socket.h/cc       # Socket封装
│       ├── Acceptor.h/cc     # 连接接受器
│       ├── TcpServer.h/cc    # TCP服务器
│       ├── TcpConnection.h/cc# TCP连接
│       ├── TcpClient.h/cc    # TCP客户端
│       ├── Connector.h/cc    # 连接器
│       ├── Buffer.h/cc       # 缓冲区
│       ├── InetAddress.h/cc  # 网络地址
│       ├── Timer.h/cc        # 定时器
│       ├── TimerQueue.h/cc   # 定时器队列
│       ├── EventLoopThread.h/cc         # 事件循环线程
│       ├── EventLoopThreadPool.h/cc     # 事件循环线程池
│       ├── Callbacks.h       # 回调函数定义
│       ├── WeakCallback.h    # 弱回调
│       └── TimerId.h         # 定时器ID
│
├── build/                    # CMake构建目录
├── lib/                      # 编译生成的库文件
│   └── liblsk_muduo.so       # 生成的共享库
├── bin/                      # 可执行文件目录
├── test_muduo/               # 测试示例
│   └── muduo_server.cpp      # 服务器示例
├── CMakeLists.txt            # CMake配置文件
├── build.sh                  # 一键编译脚本
└── README.md                 # 本文件
```

## 快速开始

### 1. 编译库

使用提供的一键编译脚本：

```bash
# Release模式编译（默认）
./build.sh

# Debug模式编译
./build.sh -d

# 清理后重新编译
./build.sh -r

# 使用8个并行任务编译
./build.sh -j 8

# 查看更多选项
./build.sh -h
```

### 2. 手动编译（可选）

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

编译成功后，会在 `lib/` 目录下生成 `liblsk_muduo.so` 共享库。

## 使用示例

### 编写服务器程序

```cpp
#include "muduo/net/TcpServer.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/InetAddress.h"
#include <functional>
#include <iostream>

using namespace std::placeholders;

class EchoServer
{
public:
    EchoServer(EventLoop* loop, const InetAddress& addr)
        : server_(loop, addr, "EchoServer")
    {
        // 设置连接回调
        server_.setConnectionCallback(
            std::bind(&EchoServer::onConnection, this, _1));
        
        // 设置消息回调
        server_.setMessageCallback(
            std::bind(&EchoServer::onMessage, this, _1, _2, _3));
        
        // 设置线程数
        server_.setThreadNum(4);
    }
    
    void start()
    {
        server_.start();
    }

private:
    void onConnection(const TcpConnectionPtr& conn)
    {
        if (conn->connected())
        {
            std::cout << "New connection from " << conn->peerAddress().toIpPort() << std::endl;
        }
        else
        {
            std::cout << "Connection closed" << std::endl;
        }
    }
    
    void onMessage(const TcpConnectionPtr& conn, Buffer* buffer, Timestamp time)
    {
        std::string msg = buffer->retrieveAllAsString();
        std::cout << "Received: " << msg << std::endl;
        conn->send(msg);  // 回显消息
    }
    
    TcpServer server_;
};

int main()
{
    EventLoop loop;
    InetAddress addr(8888);
    EchoServer server(&loop, addr);
    server.start();
    loop.loop();
    return 0;
}
```

### 编译你的程序

```bash
g++ -std=c++11 your_server.cpp -o your_server \
    -I./lsk_muduo \
    -L./lsk_muduo/lib \
    -llsk_muduo \
    -lpthread -lrt
```

或者在Windows WSL/Linux环境下：

```bash
g++ -std=c++11 your_server.cpp -o your_server \
    -I/path/to/lsk_muduo \
    -L/path/to/lsk_muduo/lib \
    -llsk_muduo \
    -lpthread -lrt

# 运行前需要设置LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/path/to/lsk_muduo/lib:$LD_LIBRARY_PATH
./your_server
```

## 技术特点

### 架构设计

- **Reactor模式**：基于事件驱动的网络编程模型
- **One Loop Per Thread**：每个线程一个事件循环，充分利用多核CPU
- **非阻塞IO**：使用epoll实现高性能IO多路复用
- **线程池**：支持多线程并发处理连接

### 核心组件

#### Base模块
- **线程管理**：Thread、ThreadPool提供线程抽象和线程池
- **日志系统**：支持同步和异步日志，多级别日志输出
- **时间工具**：高精度时间戳支持

#### Net模块
- **EventLoop**：事件循环，Reactor核心
- **Channel**：事件分发器，封装fd和感兴趣的事件
- **Poller**：IO多路复用抽象，支持epoll
- **TcpServer**：TCP服务器，支持多线程
- **TcpConnection**：TCP连接管理，自动生命周期管理
- **Buffer**：应用层缓冲区，解决TCP粘包问题
- **Timer**：定时器支持

## CMake配置说明

### 构建选项

- `CMAKE_BUILD_TYPE`：构建类型（Release/Debug）
- `BUILD_TESTS`：是否编译测试程序（默认OFF）

### 自定义安装

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local ..
make
sudo make install
```

安装后：
- 库文件安装到：`/usr/local/lib/liblsk_muduo.so`
- 头文件安装到：`/usr/local/include/muduo/`

## 编译要求

- **编译器**：支持C++11的编译器（GCC 4.8+, Clang 3.3+）
- **CMake**：3.10或更高版本
- **系统**：Linux（依赖epoll、timerfd等Linux特性）
- **依赖库**：pthread, rt

## 开发指南

### 添加新功能

1. 确定功能所属模块（base或net）
2. 在对应目录创建源文件
3. 更新 `CMakeLists.txt` 中的源文件列表
4. 遵循现有代码风格和命名规范

### 头文件引用规范

- net模块引用base模块：`#include "../base/xxx.h"`
- 同模块内引用：`#include "xxx.h"`
- 业务代码引用muduo库：`#include "muduo/net/xxx.h"` 或 `#include "muduo/base/xxx.h"`

## 性能测试

可使用 pingpong 测试或 wrk 等工具进行性能测试。

```bash
# 示例：使用wrk测试
wrk -t4 -c100 -d30s http://localhost:8888/
```

## 常见问题

### Q: Windows下如何编译？

A: 本库依赖Linux特性（epoll、eventfd等），建议在WSL或Linux虚拟机中编译和运行。

### Q: 编译时提示找不到pthread或rt库？

A: 确保链接时添加了 `-lpthread -lrt` 选项。

### Q: 运行时提示找不到.so文件？

A: 设置环境变量：
```bash
export LD_LIBRARY_PATH=/path/to/lsk_muduo/lib:$LD_LIBRARY_PATH
```

或将库文件复制到系统库目录：
```bash
sudo cp lib/liblsk_muduo.so /usr/local/lib/
sudo ldconfig
```

## 版本信息

- **版本**：1.0.0
- **C++标准**：C++11
- **协议**：参考muduo原版（BSD License）

## 参考资料

- 陈硕《Linux多线程服务端编程》
- muduo原版库：https://github.com/chenshuo/muduo

## 联系方式

如有问题或建议，欢迎提Issue。

---

**注意**：本项目仅供学习和研究使用，生产环境请充分测试。
