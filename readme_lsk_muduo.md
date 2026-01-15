二、lsk_muduo -> muduo补全关键模块
1.缺失的关键模块

1. TimerQueue 定时器系统 ⭐⭐⭐⭐⭐
原版位置: muduo/net/TimerQueue.h, Timer.h, TimerId.h
作用: 使用 timerfd_create 统一事件源，支持 runAt()/runAfter()/runEvery() 定时任务
面试考点: timerfd vs poll timeout、红黑树管理定时器、定时器回调中增删定时器
2. TcpClient + Connector 客户端支持 ⭐⭐⭐⭐⭐
原版位置: TcpClient.h, Connector.h
作用: 主动发起非阻塞connect、断线自动重连（指数退避）、处理 EINPROGRESS
面试考点: 非阻塞connect判定成功方式（可写+SO_ERROR）、自连接问题、重连策略
3. AsyncLogging 异步日志 ⭐⭐⭐⭐⭐
原版位置: AsyncLogging.h, LogFile.h, LogStream.h
当前问题: lsk_muduo的 Logger 是同步阻塞、无缓冲、无滚动
核心设计: 双缓冲技术、后台线程刷盘、日志滚动
面试考点: 双缓冲原理、多线程日志线程安全、高并发下避免日志成为瓶颈
✅ 已完成: LogStream(流式格式化) + LogFile(滚动) + AsyncLogging(双缓冲)

4. forceClose / forceCloseWithDelay ⭐⭐⭐⭐
原版位置: TcpConnection.h 中的 forceClose() 和 forceCloseWithDelay()
当前问题: lsk_muduo只有 shutdown() 优雅关闭，缺少强制关闭和超时踢人
面试考点: shutdown vs close、半关闭状态、发送缓冲区清空后关闭
5. 通用计算线程池 ThreadPool ⭐⭐⭐⭐
原版位置: ThreadPool.h
当前问题: lsk_muduo只有 EventLoopThreadPool（IO线程池），缺少CPU密集任务线程池
面试考点: IO线程池 vs 计算线程池分离、任务队列设计、线程池优雅关闭
6. Buffer高级功能 ⭐⭐⭐
原版位置: Buffer.h
缺失功能: 网络字节序读写 (readInt32/writeInt32)、findCRLF()/findEOL() 行解析、prepend() 前置写入、shrink() 收缩
面试考点: prepend空间设计原因（高效添加消息头）、网络字节序
7. 线程同步原语封装 ⭐⭐⭐
原版位置: Mutex.h, Condition.h, CountDownLatch.h
当前问题: 直接用 std::mutex，无持有者追踪和死锁检测支持
面试考点: MutexLock记录holder线程、CountDownLatch用途
8. WeakCallback 防止回调野指针 ⭐⭐⭐
原版位置: muduo/base/WeakCallback.h
作用: 使用 weak_ptr + shared_ptr 防止异步回调时对象已析构
面试考点: weak_ptr使用场景、防止悬空回调

2.修改方案
Step 1: 添加 TimerQueue 定时器系统
新增 Timer.h（封装定时任务：回调、到期时间、间隔、序号）、TimerId.h（对外标识）、TimerQueue.h/cc（使用 timerfd + std::set<pair<Timestamp,Timer*>> 管理）
在 EventLoop 中组合 TimerQueue，暴露 runAt()/runAfter()/runEvery()/cancel() 接口
TimerQueue 作为 Channel 持有 timerfd，融入 epoll 统一事件循环

Step 2: 添加 TcpClient + Connector
新增 Connector.h/cc：封装非阻塞connect状态机（kDisconnected→kConnecting→kConnected）、处理 EINPROGRESS、指数退避重连（500ms→30s）
新增 TcpClient.h/cc：组合 Connector + TcpConnection，提供 connect()/disconnect()/enableRetry() 接口
复用现有的 TcpConnection 管理已建立连接

Step 3: 升级异步日志系统 ✅ 已完成（已迁移到 lsk_muduo 目录）
新增 lsk_muduo/LogStream.h/cc（高效流式格式化，避免snprintf）、FixedBuffer<SIZE> 模板缓冲区
新增 lsk_muduo/LogFile.h/cc（带滚动的日志文件，支持按大小/时间滚动）
新增 lsk_muduo/AsyncLogging.h/cc：双缓冲设计，前端写 currentBuffer_，后端定期交换并写盘
修改 lsk_muduo/Logger.h/cc：添加 setOutput()/setFlush() 接口对接异步日志

使用方式：
```cpp
// 1. 创建异步日志实例
lsk_muduo::AsyncLogging asyncLog("myapp", 500*1000*1000); // 500MB滚动
asyncLog.start();

// 2. 在 Logger 中设置输出回调
lsk_muduo::Logger::setOutput([&asyncLog](const char* msg, int len) {
    asyncLog.append(msg, len);
});

// 3. 使用日志
LOG_INFO << "server started on port " << port;

// 4. 程序退出前停止
asyncLog.stop();
```

文件结构：
lsk_muduo/
├── LogStream.h/cc      # 流式格式化 + FixedBuffer
├── LogFile.h/cc        # 日志文件滚动
├── AsyncLogging.h/cc   # 异步日志双缓冲
└── Logger.h/cc         # 日志前端（已修改）

关键设计点：
1. **双缓冲机制**：前端写 currentBuffer_，后端定期(3秒)交换并批量写入
2. **缓冲区复用**：维护 newBuffer1/2 避免频繁内存分配
3. **防止堆积**：超过 25 个缓冲时丢弃部分消息防止内存爆炸
4. **滚动策略**：按文件大小(1GB)或时间(每天)自动滚动

Step 4: 完善 TcpConnection 连接管理
在 TcpConnection 中添加 forceClose() 直接调用 handleClose()
添加 forceCloseWithDelay(double seconds) 依赖定时器实现超时踢人
添加 setContext()/getContext() 使用 std::any 存储用户上下文
Step 5: 添加通用计算 ThreadPool
新增 ThreadPool.h/cc：包含任务队列 std::deque<Task>、条件变量同步、可配置线程数
提供 start(numThreads)/run(task)/stop() 接口
可选支持有界队列（BoundedBlockingQueue）防止任务堆积
Step 6: 增强 Buffer
在 Buffer 中添加 readInt32()/readInt64() 网络字节序读取
添加 appendInt32()/appendInt64() 网络字节序写入
添加 prependInt32() 利用 prependable 空间写消息头
添加 findCRLF()/findEOL() 文本协议解析支持
Step 7: 封装线程同步原语（可选）
新增 MutexLock.h：封装 pthread_mutex_t，记录 holder_ 线程ID，提供 isLockedByThisThread() 断言
新增 Condition.h：封装条件变量，接受 MutexLock&
新增 CountDownLatch.h：用于线程启动同步
Further Considerations
定时器实现选择：使用 timerfd 统一事件源（推荐）还是传统 poll timeout？建议 timerfd 更符合 Reactor 设计
日志级别过滤：是否在编译期还是运行期过滤低级别日志？建议运行期可配置+编译期宏优化
TcpClient重连策略：固定间隔 vs 指数退避 vs 带抖动？建议指数退避+最大上限