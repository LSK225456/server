# lsk_muduo — 仓库分拣无人叉车自主导航网关服务器

## 项目简介

基于自研 lsk_muduo 网络库（muduo 精简分支）构建的 **AGV（自动导引运输车）网关服务器**，面向"十四五"重点项目——仓库分拣无人叉车自主导航系统。

服务器采用 **Reactor + Worker 线程池** 架构，支持数百辆 AGV 同时接入。IO 线程以 50Hz 频率处理高频遥测和心跳消息，Worker 线程处理耗时业务（数据库写入、路径规划），两者通过 Task 投递机制解耦，实现快慢分离。

### 核心能力

| 能力 | 实现方式 |
|------|----------|
| 高频遥测处理 (50Hz) | IO 线程 + SpinLock 位姿更新 |
| 心跳保活 & 超时检测 | TimerQueue 看门狗（100ms 周期） |
| 低电量自动充电 | 业务引擎检测 battery < 20% 自动下发充电导航指令 |
| 多会话管理 | ConcurrentMap (shared_mutex) + SessionManager |
| 类型安全消息分发 | ProtobufDispatcher 模板化路由 |
| IO/业务线程分离 | ThreadPool Worker + runInLoop 回调 |
| 紧急制动透传 | IO 线程直接转发，不进队列，延迟可测量 |
| RTT 延迟监控 | LatencyMonitor Ping/Pong 机制（默认 5s 周期） |

### 技术栈

- **语言**: C++17
- **网络库**: lsk_muduo（Reactor 模式，epoll LT）
- **序列化**: Protocol Buffers 3
- **构建**: CMake 3.10+
- **测试**: Google Test（FetchContent 自动下载）
- **并发**: shared_mutex / SpinLock (TTAS) / ThreadPool

---

## 项目结构

```
lsk_muduo/
├── CMakeLists.txt              # 顶层构建入口
├── build.sh                    # 一键编译脚本（含依赖安装）
├── agv_server/                 # 服务器核心代码
│   ├── CMakeLists.txt          # 服务器构建配置
│   ├── GatewayMain.cc          # 服务器入口（main 函数）
│   ├── codec/                  # 编解码层
│   │   ├── LengthHeaderCodec.h
│   │   └── LengthHeaderCodec.cc
│   ├── gateway/                # 网关核心层
│   │   ├── GatewayServer.h / .cc
│   │   ├── AgvSession.h / .cc
│   │   ├── SessionManager.h / .cc
│   │   ├── ConcurrentMap.h
│   │   ├── ProtobufDispatcher.h
│   │   ├── WorkerTask.h
│   │   └── LatencyMonitor.h / .cc
│   └── proto/                  # 协议定义层
│       ├── common.proto
│       ├── message.proto
│       └── message_id.h
├── muduo/                      # lsk_muduo 网络库（base + net）
├── test_muduo/                 # 测试代码
├── bin/                        # 编译产物（可执行文件）
├── lib/                        # 编译产物（静态库）
├── build/                      # CMake 构建目录
└── logs/                       # 运行日志
```

---

## 代码文件详解

### 顶层文件

#### `CMakeLists.txt`
项目顶层 CMake 配置。设置 C++17 标准、编译选项（`-Wall -Wextra -pthread`）、输出目录（`bin/`、`lib/`）。通过 FetchContent 自动下载 Google Test。包含 `muduo/`、`agv_server/`、`test_muduo/` 三个子目录。

#### `build.sh`
一键编译脚本。支持命令行参数：`-d`（Debug 模式）、`-r`（清理重编）、`-c`（仅清理）、`-j N`（并行任务数）。自动检测并安装所需依赖（cmake、protobuf、build-essential）。首次运行时创建构建目录并执行完整的 cmake + make 流程。

---

### 编解码层 `agv_server/codec/`

#### `LengthHeaderCodec.h` / `LengthHeaderCodec.cc`
**8 字节包头 + Protobuf 负载** 的二进制协议编解码器。

协议格式：
```
+----------------+------------------+----------------+----------------------+
| Length (4字节)  | MsgType (2字节)  | Flags (2字节)  | Protobuf Payload (N) |
+----------------+------------------+----------------+----------------------+
```

- `encode()`: 将消息类型 + Protobuf 序列化数据编码为带包头的二进制帧
- `decode()`: 从 Buffer 中提取完整消息帧，解析包头和负载
- `hasCompleteMessage()`: 判断 Buffer 中是否包含至少一个完整消息（处理半包）
- `peekMessageLength()`: 窥视消息总长度（不移动读指针）

支持粘包（循环解码）、半包（等待数据完整）、超长包防护（最大 10MB）。网络字节序（大端）。

#### `CMakeLists.txt`
编译 `agv_codec` 静态库，链接 lsk_muduo_net。

---

### 协议定义层 `agv_server/proto/`

#### `common.proto`
公共枚举和基础消息类型定义：

| 枚举/消息 | 说明 |
|-----------|------|
| `TaskStatus` | 任务状态（IDLE / RUNNING / COMPLETED / FAILED / PAUSED） |
| `CommandType` | 系统指令类型（EMERGENCY_STOP / RESUME / PAUSE / REBOOT / NAVIGATE_TO） |
| `OperationType` | 操作类型（MOVE_ONLY / PICK_UP / PUT_DOWN） |
| `StatusCode` | 响应状态码（OK / INVALID_REQUEST / INTERNAL_ERROR / TIMEOUT 等） |
| `Point` | 2D 坐标点（x, y），单位米 |
| `TimedPoint` | 带时间戳的坐标点（用于轨迹预测） |

#### `message.proto`
业务消息定义：

| 消息 | 方向 | 频率 | 说明 |
|------|------|------|------|
| `AgvTelemetry` | 车→服务器 | 50Hz | 遥测数据：位姿(x,y,θ)、电量、速度、载荷、误差码、货叉高度 |
| `AgvCommand` | 服务器→车 | 事件 | 系统指令：紧急制动、恢复、暂停、导航 |
| `NavigationTask` | 服务器→车 | 事件 | 导航任务：目标点、操作类型、全局路径、任务 ID |
| `LatencyProbe` | 双向 | 5s | RTT 延迟探测：序列号、时间戳、Ping/Pong 标识 |
| `CommonResponse` | 服务器→车 | 事件 | 通用响应：状态码、消息文本 |
| `Heartbeat` | 双向 | 1Hz | 心跳保活：车辆 ID + 时间戳 |
| `MpcTrajectory` | 车→服务器 | 10Hz | MPC 控制器预测轨迹（预留） |
| `TaskFeedback` | 车→服务器 | 事件 | 任务执行反馈（预留） |
| `MessageEnvelope` | — | — | 通用消息信封（预留，用于协议扩展） |

#### `message_id.h`
消息类型 ID 常量定义与辅助函数：
- 上行消息 (0x1000-0x1FFF): `MSG_AGV_TELEMETRY` (0x1001), `MSG_MPC_TRAJECTORY` (0x1002), `MSG_TASK_FEEDBACK` (0x1003)
- 下行消息 (0x2000-0x2FFF): `MSG_AGV_COMMAND` (0x2001), `MSG_NAVIGATION_TASK` (0x2002), `MSG_LATENCY_PROBE` (0x2003)
- 通用消息 (0x3000-0x3FFF): `MSG_COMMON_RESPONSE` (0x3001), `MSG_HEARTBEAT` (0x3002)
- 辅助函数: `isUpstreamMessage()`, `isDownstreamMessage()`, `getMessageTypeName()`

类型统一使用 `uint16_t`，与 LengthHeaderCodec 包头中的 MsgType 字段对齐。

#### `common.pb.h` / `common.pb.cc` / `message.pb.h` / `message.pb.cc`
由 `protoc` 从 `.proto` 文件自动生成的 C++ 序列化/反序列化代码。构建时通过 CMake 的 `protobuf_generate_cpp` 自动重新生成。

#### `CMakeLists.txt`
查找系统 Protobuf，调用 `protobuf_generate_cpp` 生成 C++ 代码，编译 `agv_proto` 静态库。

---

### 网关核心层 `agv_server/gateway/`

#### `GatewayServer.h` / `GatewayServer.cc`
**AGV 网关服务器主类**，系统核心。

构造参数：事件循环指针、监听地址、服务器名称、会话超时时间（默认 5s）、Worker 线程数（默认 4）。

核心模块：
- **TcpServer**: 管理 TCP 连接的建立与断开
- **ProtobufDispatcher**: 根据消息类型自动路由到对应 handler
- **SessionManager**: 管理所有 AGV 车辆的会话状态
- **ThreadPool (Worker)**: 处理耗时业务（导航任务/数据库写入）
- **LatencyMonitor**: RTT 延迟监控

消息处理流程：
```
客户端数据 → onMessage() → LengthHeaderCodec 解包 → ProtobufDispatcher 分发
  ├─ AgvTelemetry (50Hz)  → handleTelemetry()  [IO 线程直接处理]
  ├─ Heartbeat (1Hz)      → handleHeartbeat()   [IO 线程直接处理]
  ├─ AgvCommand (事件)    → handleAgvCommand()  [IO 线程透传转发]
  ├─ NavigationTask (事件) → handleNavigationTask() → Worker 线程池
  └─ LatencyProbe (5s)    → handleLatencyProbe() [IO 线程处理 Pong]
```

定时器：
- **看门狗** (100ms): 遍历所有会话，超时标记 OFFLINE
- **延迟探测** (默认 5s): 向所有在线车辆发送 LatencyProbe Ping

业务引擎：
- 遥测处理时检查电量，< 20% 自动下发 `CMD_NAVIGATE_TO` 充电指令
- NavigationTask 投递到 Worker 线程，模拟 200ms 数据库写入
- Worker 完成后通过 `runInLoop` 回到 IO 线程发送 CommonResponse

#### `AgvSession.h` / `AgvSession.cc`
**AGV 车辆会话状态结构**。

每个连接的 AGV 对应一个 AgvSession 实例，包含：
- `agv_id_`: 车辆唯一标识
- `connection_`: `weak_ptr<TcpConnection>`（弱引用，不延长连接生命周期）
- `state_`: 状态枚举（ONLINE / OFFLINE / CHARGING），`std::mutex` 保护
- `battery_level_` / `last_active_time_`: `std::mutex` 保护
- `pose_x_` / `pose_y_` / `pose_theta_` / `pose_confidence_`: **SpinLock (TTAS)** 保护，支持 50Hz 高频读写无阻塞

双锁设计：
- `std::mutex`: 保护低频更新的字段（电量、状态、活跃时间）
- `SpinLock`: 保护高频更新的位姿字段（自旋锁，避免系统调用开销）

#### `SessionManager.h` / `SessionManager.cc`
**会话管理器**，封装 ConcurrentMap 提供领域语义接口。

- `registerSession(agv_id, conn)`: 创建 AgvSession 并注册
- `findSession(agv_id)`: 查找会话（返回 shared_ptr 拷贝，线程安全）
- `removeSession(agv_id)`: 按 ID 移除
- `removeSessionByConnection(conn)`: 连接断开时按连接对象反查并移除
- `forEach(callback)`: 遍历所有会话（读锁保护）
- `eraseIf(predicate)`: 条件批量删除

#### `ConcurrentMap.h`
**线程安全哈希映射**，header-only 模板类。

基于 `std::shared_mutex` 实现读写锁：
- `find()`: 读锁（shared_lock），返回 `shared_ptr` **拷贝**（而非引用/指针），即使其他线程删除原条目也不会悬挂
- `insert()` / `erase()` / `clear()`: 写锁（unique_lock）
- `forEach()`: 读锁遍历
- `eraseIf()`: 写锁 + 条件删除

#### `ProtobufDispatcher.h`
**模板化类型安全消息分发器**，header-only。

核心设计：
- `registerHandler<T>(msg_type, callback)`: 注册特定 Protobuf 消息类型的处理函数，编译期 `static_assert` 检查 T 是否为 `google::protobuf::Message` 子类
- `dispatch(conn, msg_type, data, len)`: 根据 msg_type 查找 handler，内部自动执行 Protobuf `ParseFromArray`，转发强类型消息到回调函数
- 类型擦除：`HandlerBase` 虚基类 + `TypedHandler<T>` 模板子类，运行时多态

替换了最初的 `switch-case` 硬编码分发，新增消息类型只需在 `initDispatcher()` 中添加一行 `registerHandler` 调用。

#### `WorkerTask.h`
**Worker 线程任务序列化结构**，header-only。

设计要点：
- `TcpConnectionWeakPtr conn`: 连接**弱引用**（Worker 处理期间连接可能断开）
- `AgvSessionPtr session`: 会话**强引用**（确保处理期间会话不被销毁）
- `shared_ptr<Message> message`: Protobuf 消息（堆分配，跨线程传递）
- `Timestamp enqueue_time`: 入队时间（用于计算队列延迟）

关键方法：
- `getConnection()`: `weak_ptr::lock()` 提升为 shared_ptr，失败则表示连接已断开
- `getMessage<T>()`: `dynamic_pointer_cast` 类型安全转换
- `getQueueLatencyMs()`: 计算任务在队列中等待的时间

#### `LatencyMonitor.h` / `LatencyMonitor.cc`
**RTT 延迟监控器**。

工作流程：
1. `createPing(agv_id)`: 创建 LatencyProbe 消息（Ping），atomic 递增序列号，记录到 pending 映射
2. 服务器发送 Ping → 客户端收到后原样回复 Pong（`is_response=true`）
3. `processPong(probe)`: 匹配序列号，计算 RTT = now - send_timestamp

统计数据（per-AGV）：
- `latest_rtt_ms`: 最近一次 RTT
- `avg_rtt_ms`: 平均 RTT
- `min_rtt_ms` / `max_rtt_ms`: 极值
- `sample_count`: 采样次数

安全机制：`cleanupExpiredProbes(timeout_ms)` 定期清理超时的 pending 条目（默认 30s），防止客户端不回复 Pong 导致内存泄漏。

线程安全：所有操作通过 `std::mutex` 保护，`next_seq_num_` 使用 `std::atomic`。

#### `CMakeLists.txt`
编译 `agv_gateway` 静态库，包含所有 gateway 源文件，链接 agv_codec、agv_proto、lsk_muduo_net/base、protobuf。

---

### 服务器入口

#### `GatewayMain.cc`
服务器 `main()` 函数入口。

功能：
- 命令行参数解析：`--port`（监听端口，默认 9090）、`--timeout`（会话超时秒数，默认 5.0）、`--threads`（IO 线程数，默认 0 = 单 Reactor）
- 信号处理：捕获 `SIGINT`/`SIGTERM`，执行 `loop.quit()` 优雅退出
- 创建 GatewayServer 实例并启动事件循环

#### `agv_server/CMakeLists.txt`
组装服务器：添加 proto、codec、gateway 三个子目录，创建 `gateway_main` 可执行文件，链接所有静态库。

---

## 架构设计

### 线程模型

```
┌──────────────────────────────────────────────────────┐
│                    IO 线程（Reactor）                  │
│                                                      │
│  epoll_wait → onMessage() → Codec 解包 → Dispatcher  │
│                                                      │
│  高频路径（直接处理）：                                │
│    AgvTelemetry → updatePose (SpinLock)              │
│    Heartbeat    → updateActiveTime (mutex)           │
│    AgvCommand   → 查找目标连接 → 直接转发（透传）     │
│    LatencyProbe → processPong → 更新 RTT 统计         │
│                                                      │
│  低频路径（投递到 Worker）：                           │
│    NavigationTask → 构造 WorkerTask → ThreadPool.run()│
│                                                      │
│  定时器：                                             │
│    看门狗 (100ms) → 遍历 Session → 超时标记 OFFLINE   │
│    延迟探测 (5s)  → 遍历 Session → 发送 Ping          │
└───────────────────────┬──────────────────────────────┘
                        │ Task 投递
                        ▼
┌──────────────────────────────────────────────────────┐
│              Worker 线程池（4 线程）                    │
│                                                      │
│  processWorkerTask():                                │
│    1. weak_ptr.lock() 检查连接有效性                   │
│    2. 模拟数据库写入 (200ms usleep)                    │
│    3. runInLoop() 回到 IO 线程发送响应                 │
└──────────────────────────────────────────────────────┘
```

### 协议交互时序

```
  AGV Client                    Gateway Server
      │                              │
      │──── AgvTelemetry (50Hz) ────►│  IO 线程更新 Session
      │                              │
      │◄─── Heartbeat ──────────────│  回复心跳
      │──── Heartbeat ─────────────►│  刷新 last_active_time
      │                              │
      │◄─── AgvCommand ────────────│  低电量自动充电
      │     (CMD_NAVIGATE_TO)        │
      │                              │
      │◄─── LatencyProbe (Ping) ───│  RTT 探测
      │──── LatencyProbe (Pong) ───►│  计算 RTT
      │                              │
      │──── NavigationTask ────────►│  投递到 Worker
      │                              │  ↓ 200ms DB 写入
      │◄─── CommonResponse ────────│  IO 线程发送响应
      │                              │
  AGV-A                          Gateway
      │──── AgvCommand ───────────►│  紧急制动透传
      │     (target=AGV-B)          │  ↓ IO 线程直接转发
      │◄─── CommonResponse ────────│  回复发送方
      │                          AGV-B
      │                     ◄──── AgvCommand ──│
      │                           (EMERGENCY_STOP)
```

---

## 构建与运行

### 依赖

- GCC/G++ 9+（C++17 支持）
- CMake 3.10+
- Protocol Buffers 3（libprotobuf-dev + protobuf-compiler）
- pthread

### 一键编译

```bash
chmod +x build.sh
./build.sh              # Release 模式编译
./build.sh -d           # Debug 模式编译
./build.sh -r           # 清理后重新编译
./build.sh -j 8         # 使用 8 个并行任务
./build.sh -c           # 仅清理编译产物
```

### 手动编译

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 启动服务器

```bash
./bin/gateway_main --port 9090 --timeout 5.0 --threads 2
```

### 运行测试

```bash
./bin/test_lsk_server          # 综合测试（32 个用例，覆盖迭代 1-3）
./bin/buffer_test              # Buffer 整数操作测试
./bin/codec_test               # 编解码测试（粘包/半包/空载荷）
./bin/proto_test               # Protobuf 序列化测试
./bin/dispatcher_test          # 消息分发器测试
./bin/concurrent_map_test      # 线程安全容器测试
./bin/session_manager_test     # 会话管理测试
./bin/worker_task_test         # Worker 任务投递测试
./bin/fast_slow_separation_test # 快慢分离验证测试
```

---

## 迭代进度

| 迭代 | 周次 | 状态 | 核心产出 |
|------|------|------|----------|
| 迭代一 | 1-2 | ✅ 完成 | Buffer 整数操作、Protobuf 协议、LengthHeaderCodec、GatewayServer 骨架、双闭环安全 |
| 迭代二 | 3-4 | ✅ 完成 | ProtobufDispatcher、ConcurrentMap、SessionManager、心跳超时、多客户端联调 |
| 迭代三 | 5-6 | ✅ 完成 | WorkerTask 投递、快慢分离、紧急制动透传、LatencyMonitor、200ms 业务阻塞验证 |
| 迭代四 | 7-8 | 🔜 待开始 | LoadTester 压测模拟器、Statistics 统计、渐进式压测、火焰图、文档 |
