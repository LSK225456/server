一、muduo模块解析
1.noncollectable
基类，派生类对象无法进行拷贝构造和赋值
TCP 连接在逻辑上是独占的，不能被拷贝

2.日志
旧：INFO,ERROR,FATAL,DEBUG五类日志，FATAL直接退出
LogStream (格式化层)  ->  AsyncLogging (异步层 - 双缓冲)  ->  LogFile (文件层 - 滚动)






3.Timestamp
返回当前时间

4.InetAddress
数据包装类，不负责发送接收数据，只负责管地址。
对外提供两个不同的构造函数：主动创建（port，ip）；被动接收(const sockaddr_in &addr)
toIp：返回ip
toPort：返回端口号
toIpPort：返回ip：端口号
getSockAddr：返回sockaddr_in
setSockAddr(const sockaddr_in &addr)：设置sockaddr_in

5.Channel
一个channel绑定一个fd，以及存储该fd所有events_事件，感兴趣的revents_

setReadCallback，setWriteCallback，setCloseCallback，setErrorCallback：接口函数，把外部调用的回调赋给类内变量。

tie(const std::shared_ptr<void>&)：TcpConnection 在创建时，把自己（shared_from_this）通过 tie 传给 Channel。把传进来的 shared_ptr（强引用）赋值给 weak_ptr（弱引用）。如果TcpConnection 正在析构（销毁）。但在销毁的瞬间，epoll 刚好触发了一个事件，调用了 Channel::handleEvent。handleEvent会检查TcpConnection是否还存活

handleEvent(Timestamp receiveTime)：生死检查，如果TcpConnection还活着：lock() 成功，返回一个有效的 shared_ptr。此时引用计数 +1，对象在 guard 销毁前绝对不会死。Lock成功就进入回调，失败什么都不做

handleEventWithGuard(Timestamp receiveTime)：回调分支，根据这个实例的revents_情况调用不同回调。

set_revents(int revt)：设置revents_

enableReading，disableReading，enableWriting，disableWriting，disableAll：设置events_，是否允许读、写，后面都调用update

remove，update：调用该实例所属的loop的removeChannel或updateChannel

isNoneEvent，isWriting，isReading：判断该channel是否对读/写感兴趣

6.Poller
是个抽象类
有成员变量std::vector<Channel*>，std::unordered_map<int, Channel*>
只提供三个纯虚函数接口poll（等待事件）、updateChannel（添加/修改事件）、removeChannel（移除事件）
hasChannel(Channel *channel)：查找哈希表里是否有该channel
newDefaultPoller(EventLoop *loop)：return new EPollPoller(loop);也可以实现返回poller

7.EPollPoller
当一个 Channel 暂时不想监听任何事件（events == kNoneEvent），updateChannel 会调用 epoll_ctl(EPOLL_CTL_DEL) 把通过内核移除监听，并将状态设为 kDeleted。 但是此时代码并没有从哈希表中把这个 Channel 删掉。
提供poll，updateChannel，removeChannel具体实现
activeChannels（std::vector<Channel*>）：存储活跃的channel，后续给EventLoop循环调用channel->handleEvent
ChannelMap 维护的是“当前所有注册在 Poller 中的 Channel”。它的修改（增/删）是在事件处理回调中发生的。

（1）create：EPollPoller构造时调用::epoll_create1返回给epollfd_
（2）epoll_ctl注册：updateChannel/removeChannel、update (private)
（3）epoll_wait及结果处理：poll / fillActiveChannels

updateChannel(Channel *channel) ：根据channel的index执行不同操作（kNew：添加到哈希表，调用update更新该channel；kDeleted：直接调用update更新该channel；kAdded：先判断isNoneEvent再调用update）

removeChannel(Channel *channel) ：从哈希表中删除，调用update

update(int operation, Channel *channel)：真正执行epoll_ctl的地方，获取channel的events，根据不同operation调用epoll_ctl

poll(int timeoutMs, ChannelList *activeChannels) ：epoll_wait修改events_（就是revents）数组，正常时调用fillActiveChannels，感兴趣事件太多时对events_扩容

fillActiveChannels(int numEvents, ChannelList *activeChannels)：根据epoll_wait修改的events_数组，提取出每一个channel指针，设置每一个channel的感兴趣事件revents，并把channel添加进activeChannels

8.CurrentThread
TLS，线程局部存储，每个线程一个id
Tid：返回当前线程的id

9.EventLoop
持有activeChannels_活跃的channel数组，std::vector<Functor>待执行的回调函数数组
有一个线程级别的变量__thread EventLoop *t_loopInThisThread保证一个线程只有一个loop

runInLoop的语义是：如果在IO线程，立即执行；否则投递到队列。
queueInLoop的语义是：无论在不在IO线程，都投递到队列，等当前回调执行完再执行。

EventLoop()：获取该线程id，new poller_，唤醒wakeupFd_并打包为channel，给该channel设置读回调handleRead，设置可读

~EventLoop()：销毁wakeupChannel_，关闭wakeupFd_，t_loopInThisThread指向空

createEventfd：eventfd方法获取wakeupFd_

loop()：while(!quit_)下执行1.清空activeChannels_，2.poller_->poll获得活跃的activeChannels_，给每个活跃channel调用channel->handleEvent（即进入调用具体回调部分代码），3.doPendingFunctors()

doPendingFunctors()：互斥锁（和queueInLoop互斥），临时变量std::vector<Functor>和pendingFunctors_ swap之后逐个执行，执行完后callingPendingFunctors_设为false

quit()：设置quit_为true，如果在本线程loop()退出while循环，不是的话就调用wakeup

Wakeup，handleRead：给该wakeupFd_执行系统级api，write和read

runInLoop(Functor cb)：若在本线程直接执行回调，不在丢给queueInLoop

queueInLoop(Functor cb)：锁保护cb传入pendingFunctors_，后wakeup

新版muduo：新增持有一个timerQueue_成员，对外暴露四个定时器接口：runAt、runAfter、runEvery、cancel
runAt(Timestamp time, Functor cb)：在指定时间运行回调
runAfter(double delay, Functor cb)：在 delay 秒后运行回调
runEvery(double interval, Functor cb)：每隔 interval 秒运行一次回调
cancel(TimerId timerId)：取消定时器



10.Thread
Thread(ThreadFunc func, const std::string &name)：构造时EventLoopThread 在初始化 thread_ 时，会把自己的成员函数 threadFunc 传进去。这个 threadFunc 里通常就干两件事：在栈上创建一个 EventLoop 对象。调用 loop.loop() 开始事件循环。

start()：用std::shared_ptr<std::thread>开启新子线程，子线程先获取其id，然后执行上述说的func。使用sem_t信号实现同步

join()：当主线程调用 t.join() 时，主线程会阻塞，直到线程 t 执行完它的 func_() 函数并退出。

11.EventLoopThread
启动一个新线程，并在那个新线程中构建并运行一个 EventLoop，最后把这个 Loop 的指针返回给主线程。
std::condition_variable条件变量，
EventLoopThread：构造时，loop_指向空，实例化thread_绑定threadFunc，绑定上层传来的回调callback_

~EventLoopThread()：loop_->quit(); thread_.join();

startLoop()：先thread_.start()开启子线程，利用锁和条件变量获取该子线程启动的loop的指针然后返回该指针

threadFunc()：EventLoopThread构造时实例化thread_绑定threadFunc，然后startLoop函数调用时，thread_.start()开启的子线程就会执行这个threadFunc。
首先实例化一个新的loop，调用上层给EventLoopThread传递的初始化回调，执行完后利用锁和条件变量把该loop的指针传给成员变量loop_，最后开始loop.loop()。整个服务端流程会一直停在这一行。

12.EventLoopThreadPool
存有主循环指针baseLoop_，可根据cpu核数设置numThreads_个线程。
有std::vector<std::unique_ptr<EventLoopThread>>和std::vector<EventLoop*>

start(const ThreadInitCallback &cb)：循环实例化EventLoopThread，并逐个存入成员变量，执行EventLoopThread -> startLoop。如果单线程，在主线程直接执行回调


getNextLoop：轮询子线程来分配新连接，返回下一个loop的指针

13.Socket
sockfd 的拥有者。在构造函数中，它接收一个由系统 API创建的 sockfd。
在析构函数中，它调用close(sockfd_)。这确保了无论程序如何退出（包括异常），文件描述符都不会泄漏。
封装bind, listen, accept, setsockopt，shutdown方法

14.Buffer
核心是为了解决非阻塞 IO 中数据读取效率与内存占用的矛盾。它使用 vector 保证内存连续性方便解析，利用 readv 系统调用结合栈上 64K 缓冲区，实现了在不预先分配大内存的前提下，一次 syscall 尽可能读取更多数据。同时，内部通过 prependable 空间优化了包头添加的拷贝开销，通过内部移动数据（makeSpace）减少了 resize 的频率。

retrieve：复位双指针
retrieveAllAsString：把读到的数据转为string并复位
makeSpace(size_t len)：buffer扩容函数，如果可读段有空闲空间，用copy函数把可读段的缓存移到包头后面的连续内存，如果该空闲空间不够，才扩容
append(const char* data, size_t len)：检查是否需要扩容，然后copy
readFd(int fd, int *saveErrno)：在栈上创建一个64k内存，使用readv函数，如果当前buffer的可写区够用，那么用不着栈上内存。不够用才往里存并调用append

15.TcpConnection
TcpConnection构造：根据Tcpserver给的sockfd新连接听fd，构造
socket和channel_实例，设置channel的读、写、关闭、错误回调


send(const std::string &buf)：若此时在本线程直接调用sendInLoop，不在就loop_->runInLoop

sendInLoop(const void* data, size_t len)：如果该channel不可写且outputbuffer无缓冲数据，调用系统级write写数据，如果写完了就调用writeCompleteCallback_，如果没写完就往outputBuffer_里填，检测是否触发水位线，若触发调用回调

Shutdown：关闭套接字

connectEstablished和connectDestroyed：tcpserver调用的，有新连接时把channel设置tie并设置可读，调用connectionCallbac；销毁时channel_->disableAll同时也会调用connectionCallback

handleRead(Timestamp receiveTime)：先inputBuffer_.readFd然后调用用户自定义的messageCallback

handleWrite：如果channel可写，outputBuffer_.writeFd、复位；如果outputBuffer缓冲区空了就设置channel不可写且调用用户设置的writeCompleteCallback

handleClose：调用用户定义的connectionCallback和tcpserver实现的closeCallback

本地：shutdown(SHUT_WR) → 发送FIN包 → 进入FIN_WAIT状态
对端：收到FIN → 知道本地不再发送数据 → 可以继续发数据给本地
本地：还可以接收对端数据（半关闭状态）
对端：最终也关闭 → 发送FIN → 本地收到 → 连接完全关闭
shutdown只关闭写端，是半关闭。发送缓冲区里的数据会发完，本地还能接收对端数据。这是优雅关闭，给对端时间处理收尾逻辑。



forceClose：立即、彻底、无条件地关闭这个连接，丢弃一切未发送数据。
forceCloseWithDelay(double seconds)：seconds秒后关闭
16.Tcpserver


17.ThreadPool

ThreadPool：管理多个 Worker 线程，执行耗时任务

start(int numThreads)：启动线程池，创建 N 个工作线程，numThreads=0 时，任务在调用者线程直接执行

run(Task task)：提交任务到线程池。如果是有界队列且已满，等待队列有空位（使用一个条件变量，take时会唤醒run所在线程），提交之后通知一个等待的消费者线程（用另一个条件变量）

Stop：唤醒所有等待的工作线程，等待所有线程退出（join）

runInThread：工作线程的主循环，首先执行线程初始化回调，然后while循环一直take队列里的任务（可能阻塞），take之后就执行

take：从队列取任务，如果是有界队列，用条件变量通知run可以继续提交

18.TimerQueue + Timer + TimerId（定时器系统）
Timer ：一个定时器单位，保存了一个定时器应有的信息（定时器回调、到期时间，重复间隔，序号等）
restart(Timestamp now);        // 重新计算下一次超时时间（仅用于重复定时器）


TimerQueue数据结构：
typedef std::pair<Timestamp, Timer*> Entry;
typedef std::set<Entry> TimerList;		按时间排序的定时器列表（红黑树），自动排序，快速找到下一个要触发的定时器，timers_.begin() 永远是最早到期的定时器

typedef std::pair<Timer*, int64_t> ActiveTimer;
typedef std::set<ActiveTimer> ActiveTimerSet;   按指针排序，快速取消指定定时器


addTimer(TimerCallback cb, Timestamp when, double interval)：添加定时器，在runInLoop绑定addTimerInLoop

addTimerInLoop() - 实际把新增的定时器插入TimerList，如果新插入的定时器排在了最前面，说明我们需要修改内核的 timerfd 设置，让它在更早的时间唤醒

insert() - 插入红黑树TimerList和ActiveTimerSet，如果该定时器时间最近，返回true

createTimerfd：向操作系统申请一个定时器文件描述符

TimerQueue构造：申请定时器fd，创建channel，setReadCallback->handleRead

handleRead：获取所有已到期的定时器，调用到期定时器的回调函数


getExpired(Timestamp now)：二分查找-获取到期定时器，并从TimerList和ActiveTimerSet中删除

reset(const std::vector<Entry>& expired, Timestamp now)：处理完一波任务后，有的任务要销毁，有的要循环，最后还要重新定闹钟。
for (timer : expired) {
    是重复定时器？
        ├─ 是 -> 在取消列表中？
        │           ├─ 是 -> delete（被取消了）
        │           └─ 否 -> restart(now)  // 重新插入
        └─ 否 -> delete（一次性定时器）
}
重置 timerfd 为下一个最早的定时器

cancelInLoop(TimerId timerId)：使用ActiveTimerSet搜索删除，如果定时器还在队列中（未到期），直接删，如果定时器不在队列中，但正在“处理过期任务”的状态，就加入取消列表中，reset时会处理删除

19.TcpClient + Connector（客户端支持）

Connector
connect() ：创建socketfd，发起非阻塞connect，根据错误码分类处理

handleWrite()：当 epoll 通知 socket 可写时，可能有三种情况：连接成功。连接失败（例如被拒绝），socket 也是可写的。自连接

retry(int sockfd)：关闭旧的socket，如果用户还想连，使用定时器实现延迟重连：tartInLoop 会再次调用 connect()。指数退避，但不超过最大延迟   下一次延迟加倍：0.5s -> 1s -> 2s ... 直到 30s

// kConnecting: 正在连接中（调用了 ::connect 但还没返回成功，正在 epoll_wait 写事件
// kConnected: 连接已建立（一旦建立，Connector 的任务就完成了，控制权移交给 TcpConnection）


TcpClient
newConnection(int sockfd)：当 Connector 成功连上 socket 后，会调用此函数。这是 Connector -> TcpClient 的交接。创建 TcpConnection，把用户设置的回调（新连接，新信息，写完，关闭）设置给TcpConnection


removeConnection(const TcpConnectionPtr& conn)：销毁连接，如果启用重连，重新连接connector_->restart()



20.spinlock自旋锁
适用场景：锁持有时间极短（<10μs），竞争不激烈
SpinLock：
Lock：TTAS算法：
     *       1. 先用load测试锁是否空闲（不修改cache line）
     *       2. 空闲时才用exchange尝试获取（CAS操作）
     *       3. 失败则继续自旋

Unlock：解锁
tryLock：尝试加锁，非阻塞

SpinLockGuard：SpinLock的包装器，构造时加锁，析构时解锁

21.weakcallback
异步回调的野指针问题：回调注册时对象还活着，回调触发时对象可能已经死了


核心思想：我不持有你的强引用（不影响你的生命周期），但我可以在回调触发时先检查你是否还活着，活着就调用，死了就什么都不做。

该类持有std::weak_ptr<CLASS> object_;                       // 弱引用目标对象
std::function<void (CLASS*, ARGS...)> function_;    // 成员函数包装
















































二、叉车业务层模块
1.Protobuf
2.LengthHeaderCodec编解码器
协议格式：Length (4字节) | MsgType (2字节)  | Flags (2字节)  | Protobuf Payload (N) |
encode(lsk_muduo::Buffer* buf, uint16_t msgType,const std::string& protoData,uint16_t flags)：把buf填满，按照包头包体顺序

hasCompleteMessage：先确认 Buffer 里至少有 8 字节包头，读取 Length 字段，再判断 Buffer 里的可读字节数是否 >= Length。这样就能精确地判断"当前 Buffer 是否包含至少一个完整消息"，处理完一条就继续循环处理下一条，优雅解决粘包。

decode：先hasCompleteMessage判断，然后依次读出各部分数据，传出参数

encode(lsk_muduo::Buffer* buf,uint16_t msgType,const std::string& protoData,uint16_t flags)：直接把所有数据填入buf



3.GatewayServer网关服务器
AgvSession类：代表业务层面的“车辆”。它关心的是电量、位置、ID等单车信息，一个实例代表一个车，和TcpConnection是一对一关系。提供查询修改车辆信息的接口，用同一个mutex锁着所有函数

GatewayServer类：主服务端。持有一个baseloop，server，map<std::string, AgvSessionPtr>，map<std::string, lsk_muduo::TcpConnectionPtr>


GatewayServer(EventLoop* loop,const InetAddress& listen_addr,const std::string& name)构造：
1.构造baseloop和server，注册 TcpServer 连接变动回调和新信息回调，调用的都是本类内部实现的函数onConnection，onMessage。
2.initDispatcher() —— 在服务器启动前一次性注册所有消息类型的 handler
3.启动 Worker 线程池，threadpool->start()

initDispatcher：一次性注册所有消息类型的 handler，比如处理高频遥测信息，心跳信息、导航任务信息，也就是具体的业务层，都实现在GatewayServer

Start：server_.start()；启动100ms定时器onWatchdogTimer；启动延迟探测定时器onLatencyTimer，onLatencyTimer具体内容在LatencyMonitor 里讲解

onConnection(const TcpConnectionPtr& conn)：建立新连接（懒连接，收到第一次遥测信息才真正连接）或者销毁连接（线程安全，遍历 connections_，找到对应的 agv_id 并清理两个map）

removeSessionByConnection：连接断开时调用 removeSessionByConnection，但注意它是委托给 SessionManager 的，而 SessionManager 里又委托给 ConcurrentMap::eraseIf，整个调用链是 O(n) 遍历——当在线车辆数量 n 很大时（比如 1000 辆），每次断开连接都要遍历。

onMessage(const TcpConnectionPtr& conn,Buffer* buf,Timestamp receive_time)：while循环来处理buf，循环处理粘包问题。while (LengthHeaderCodec::hasCompleteMessage(buf))。decode解码，然后// 使用 ProtobufDispatcher 分发消息（替换原 switch-case）
dispatcher_.dispatch(conn, msg_type, payload.data(), payload.size());

handleTelemetry(const TcpConnectionPtr& conn,const proto::AgvTelemetry& msg)：1.查找或创建会话,懒加载注册,第一次收到消息时，才建立 Session  2. 更新本地维护的车辆状态  3. 触发基础业务引擎（目前只有低电量触发充电功能）

handleHeartbeat(const TcpConnectionPtr& conn,const proto::Heartbeat& msg)：回复车辆心跳信息

handleNavigationTask(const TcpConnectionPtr& conn,const proto::NavigationTask& msg)：迭代三新增的复杂任务处理模拟（模拟a*路径规划或者数据库写入等复杂耗时业务，目前用sleep模拟）


handleAgvCommand() —— IO 线程透传，绝不入队，EMERGENCY_STOP 等高优先级指令的延迟不可预期，必须在 IO 线程立刻处理。

registerSession(const std::string& agv_id,const TcpConnectionPtr& conn)：建立车辆连接，更新两个map

onWatchdogTimer：遍历map<std::string, AgvSessionPtr>，如果超过一段时间没有接收到车辆信息，就设置为offline

4. test_muduo/MockAgvClient模拟叉车客户端
目前有电量模拟，状态机切换，指令响应，自动发送车辆状态(50Hz), Heartbeat(500ms)功能
构造：设置TcpClient回调：连接onConnection，新消息onMessage

onConnection(const TcpConnectionPtr& conn)：如果连接上了，在该车所在的loop上启动定时器：发送车辆状态、心跳、电量更新、检查是否接受server消息；如果没有连接上，直接设置停止

onMessage(const TcpConnectionPtr& conn,Buffer* buf,Timestamp receive_time)：同样循环处理粘包，解码，处理protobuf

handleProtobufMessage：根据msgtype调用不同业务接口

handleAgvCommand：根据数据包体的不同内容设置车辆状态


startMovingToCharger：延迟3秒后自动更新至充电状态
onBatteryTimer：根据车辆不同状态调控电量
onWatchdogTimer：如果last_server_msg_time_-now超过阈值，断定为失联，停止，last_server_msg_time_在每次收到消息会更新


5. workertask类

WorkerTask 解决的核心问题是：如何安全地把一个与特定连接、特定车辆绑定的任务跨线程传递。它是 IO 线程和 Worker 线程之间的"信封"，把任务需要的所有上下文打包在一起。

conn 弱引用、session 强引用：如果 WorkerTask 持有 conn 的强引用 shared_ptr，那么即使连接已经"逻辑上断开"，TcpConnection 对象也无法析构，对应的 socket fd 被占着不放，这是资源泄漏。AgvSession 在连接断开后不应该立即销毁。它存储了这辆车的历史状态（位置、电量、任务信息），要支持车辆重连时复用。更重要的是，Worker 线程在执行期间需要读取 session 里的数据（agv_id、位姿等），如果 session 随时可能析构，Worker 线程就相当于拿着一个随时可能变野的指针，会崩溃。



6.ProtobufDispatcher 分发器
TypedHandler：是一个模板派生类，它把"反序列化为哪种类型"这个信息编码进模板参数里。通过虚函数 handle() 从外部统一调用，内部自动完成强类型反序列化和回调，实现了类型安全的多态分发，避免了 switch-case 里 static_cast 的危险。
handle：1. 创建 MessageT 实例，2. ParseFromArray 反序列化，3. 成功后调用 callback_(conn, msg)

ProtobufDispatcher：
成员变量
//msg_type -> Handler 映射（运行时只读，无需加锁）
std::map<uint16_t, HandlerPtr> handlers_;

dispatch：1. 根据 msg_type 查找注册的 handler  2. handler 内部完成反序列化 3. 反序列化成功后调用强类型回调 4. 未找到 handler 则调用 defaultCallback（若有）
GatewayServer::onMessage 里的调用dispatch：
dispatcher_.dispatch(conn, msg_type, payload.data(), payload.size());

registerHandler(uint16_t msg_type,std::function<void(const lsk_muduo::TcpConnectionPtr&,
const MessageT&)> callback)：注册消息处理函数（核心模板接口）
GatewayServer::initDispatcher()会调用：
dispatcher.registerHandler<AgvTelemetry>(MSG_AGV_TELEMETRY,std::bind(&GatewayServer::handleTelemetry, this, _1, _2));


7.ConcurrentMap读写锁
持有std::unordered_map<Key, ValuePtr> map_;，键值对是模版
线程安全保证：
 *       - insert / erase / clear -> unique_lock（写锁，独占）
 *       - find / contains / size / forEach -> shared_lock（读锁，并发）
 *       - find 返回 shared_ptr 拷贝，拷贝完成后释放锁，不会长期持锁

- unordered_map 平均 O(1) 查找/插入/删除
 *       - shared_mutex 在读多写少场景下优于 mutex

与 std::map 的区别：
 *       - std::map 有序（红黑树），unordered_map 无序（哈希表）
 *       - 会话管理场景不需要有序遍历，哈希表更快

forEach(std::function<void(const Key&, const ValuePtr&)> func) const：安全遍历所有元素并给每个元素调用func回调，但是回调中不能修改元素

eraseIf(std::function<bool(const Key&, const ValuePtr&)> predicate)：在写锁保护下遍历并可删除元素，适用场景：看门狗批量清理超时会话，遍历在predicate函数中判断每个元素，符合条件即删除

8.AgvSession车辆
代表一辆叉车在服务端的完整状态快照。它不负责通信，只负责存数据。它的生命周期由 SessionManager 通过 shared_ptr 管理。

持有的成员变量conn_ 是 weak_ptr，构造时传入 shared_ptr 会隐式转换，引用计数不增加。
为什么不存 shared_ptr？因为 TcpConnection 的生命周期应该完全由 muduo 的 TcpServer 控制，连接断开时应该让引用计数归零正常析构。如果 AgvSession 持有强引用，即使 TcpServer 已经移除了连接，连接对象也无法析构，造成资源泄漏。

getConnection()：获取连接对象（提升为 shared_ptr），如果连接仍然有效return shared_ptr ，否则返回空指针
9.SessionManager 会话管理器
SessionManager 是 GatewayServer 和 ConcurrentMap 之间的薄层，它本身无状态，线程安全完全由下层 ConcurrentMap 的读写锁保证。核心亮点是 removeSessionByConnection 的反向查找：连接断开时只有连接对象，没有 agv_id，通过 eraseIf 在写锁内原子地遍历比较连接指针地址，找到并删除对应会话，避免了"先查找再删除"的 TOCTOU 竞态窗口。
GatewayServer（网络 + 业务逻辑）

    └── SessionManager（会话生命周期管理）
            └── ConcurrentMap<string, AgvSession>（线程安全容器）
                    └── AgvSession（数据实体，持有 weak_ptr<TcpConnection>）

// 会话容器：agv_id -> AgvSession（线程安全）
ConcurrentMap<std::string, AgvSession>

registerSession(const std::string& agv_id,const std::shared_ptr<lsk_muduo::TcpConnection>& conn)：注册新会话，先读操作，看是否之前已注册过，否则写操作insert

removeSessionByConnection(const std::shared_ptr<lsk_muduo::TcpConnection>& conn)：根据连接对象反查并移除会话。当连接断开时，GatewayServer::onConnection 收到断开通知，此时只有 TcpConnectionPtr conn，不知道对应哪辆车的 agv_id——因为业务 ID 和底层连接的对应关系只记录在 AgvSession 里，而 session 是用 agv_id 做 key 存的。所以必须"反向查找"：遍历所有 session，找到哪个 session 的连接对象跟传入的 conn 是同一个对象，然后删掉它。

forEach(std::function<void(const std::string&, const AgvSessionPtr&)> func) const：看门狗的遍历接口。遍历所有会话，应用场景：看门狗定时器遍历检查超时，统计模块收集所有车辆状态。GatewayServer::onWatchdogTimer()会调用这个函数然后遍历检查超时


10.LatencyMonitor RTT 监控器
解决的问题是：服务器如何主动测量自己到每辆叉车之间的网络往返延迟（RTT），而不依赖客户端主动上报。
服务器发一个带序列号和时间戳的 Ping，客户端原样回一个 Pong，服务器收到 Pong 后用当前时间减去发送时间得到 RTT。整个测量的时间在服务器侧完成，不依赖客户端的系统时钟，避免了时钟漂移问题。

std::unordered_map<uint64_t, std::pair<std::string, int64_t>>：是"待匹配账本"——每发出一个 Ping，就往这里登记一条记录：key 是 seq_num，value 是发给哪辆车以及什么时间发的。当 Pong 回来后，用 seq_num 查这张表，查到了就知道 RTT，然后从表里删掉（消账）。如果 Pong 一直没来，这条记录就一直在表里，需要定期清理防止内存泄漏。

createPing(const std::string& target_agv_id)：return 构造好的 LatencyProbe 消息

processPong(const proto::LatencyProbe& pong)：处理 Pong 响应，返回计算 RTT

cleanupExpiredProbes(double timeout_ms = 30000.0)：清理超时的 pending 探测条目，由 onLatencyTimer 定期调用，避免客户端不回复 Pong 导致内存泄漏，遍历清除

logAllStats：输出所有 RTT 统计到日志

onLatencyTimer() 里的完整调用顺序（gatewayserver里循环调用的）：
步骤一：给所有在线车辆发 Ping（在sessionmanager的foreach下遍历的）  步骤二：把当前所有车辆的 RTT 统计打印到日志  步骤三：清理超时的 pending 条目



