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
✅ 已完成: forceClose() + forceCloseWithDelay() + WeakCallback.h（防止回调野指针）
5. 通用计算线程池 ThreadPool ⭐⭐⭐⭐
原版位置: ThreadPool.h
当前问题: lsk_muduo只有 EventLoopThreadPool（IO线程池），缺少CPU密集任务线程池
面试考点: IO线程池 vs 计算线程池分离、任务队列设计、线程池优雅关闭
✅ 已完成: ThreadPool.h/cc（支持有界/无界队列、优雅关闭、线程初始化回调）
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

