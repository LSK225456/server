# lsk_muduo

## 1. 项目简介

`lsk_muduo` 是一个面向仓库分拣无人叉车场景的 AGV 网关服务器项目，核心目标是把“车辆高频上报、服务器状态监管、控制指令回传、会话生命周期管理、业务线程隔离、链路延迟观测”整合成一套可运行、可测试、可扩展的完整通信系统。

这个仓库的主体并不是单纯写一个业务服务器，而是同时做了两层工作：

1. **在底层补全并扩展 muduo 精简版网络库**，补上 `TimerQueue`、`TcpClient`、`Connector`、`ThreadPool`、`SpinLock`、`WeakCallback`、增强版 `Buffer`、异步日志等关键模块。  
2. **在上层实现 AGV 网关业务**，完成 Protobuf 协议、长度头编解码、网关服务端、模拟客户端、会话管理、快慢分离、延迟监控和综合测试。

从项目定位上看，它不是一个“只会收发 socket 的 demo”，而是一个围绕以下真实工程问题展开的通信系统：

- AGV 持续高频上报遥测数据时，服务器如何稳定接收并更新内存状态。
- 车辆掉线、服务器失联、消息半包、粘包、重载、低电量等场景如何处理。
- 高频轻业务和低频重业务如何拆开，避免耗时逻辑阻塞 IO 线程。
- 紧急制动这类高优先级控制指令如何走最快路径，避免排队。
- 如何用统一协议和完整测试，把服务端、客户端、网络库三层串成一个闭环。

---

## 2. 核心功能详细介绍

### 2.1 网络与并发架构

项目采用 **Reactor + Worker 线程池** 的结构：

- **IO 线程**负责网络连接、读写事件、解包、消息分发和高频轻量逻辑。
- **Worker 线程池**负责耗时业务，例如模拟数据库写入。
- **TimerQueue** 负责心跳检测、看门狗、延迟探测等周期任务。
- **runInLoop / queueInLoop** 负责跨线程安全回调，把 Worker 处理结果送回 IO 线程发送。

这意味着：

- 高频消息不会因为慢业务被拖住。
- 网络层和业务层边界清晰。
- 既可以单 Reactor 运行，也可以通过 `--threads` 扩展为多 IO 线程模式。

### 2.2 协议层：8 字节长度头 + Protobuf

项目通信协议采用固定 8 字节包头配合 Protobuf 负载，格式如下：

```text
+----------------+------------------+----------------+----------------------+
| Length (4字节) | MsgType (2字节)  | Flags (2字节)  | Protobuf Payload (N) |
+----------------+------------------+----------------+----------------------+
```

协议层解决了以下问题：

- **半包处理**：数据没收全时等待，不误解析。
- **粘包处理**：同一个 buffer 里有多条消息时循环解包。
- **消息分类**：通过 `MsgType` 精确路由到不同业务处理函数。
- **安全保护**：超过 10MB 的异常消息直接拒绝。
- **可扩展性**：`Flags` 预留给压缩、加密、优先级等扩展能力。

### 2.3 消息体系

协议中定义了三大类消息：

1. **上行消息（车辆 -> 服务器）**
  - `AgvTelemetry`：高频遥测，上报位置、电量、速度、误差码等。
  - `MpcTrajectory`：预测轨迹，当前预留。
  - `TaskFeedback`：任务反馈，当前预留。

2. **下行消息（服务器 -> 车辆）**
  - `AgvCommand`：紧急制动、恢复、暂停、导航。
  - `NavigationTask`：导航任务与全局路径。
  - `LatencyProbe`：RTT 探测 Ping/Pong。

3. **通用消息（双向）**
  - `Heartbeat`：心跳保活。
  - `CommonResponse`：对控制指令或任务的统一响应。

### 2.4 会话管理与车辆状态管理

每台 AGV 在服务端都有一个 `AgvSession` 对象，里面维护：

- `agv_id`
- `TcpConnection` 的弱引用
- 最后活跃时间
- 电量
- 当前状态（`ONLINE` / `OFFLINE` / `CHARGING`）
- 位姿信息（`x / y / theta / confidence`）

这里的设计很工程化：

- **连接用 weak_ptr**，避免业务层把底层连接生命周期“拖住”。
- **普通字段用 mutex**，保护电量、状态、最后活跃时间。
- **位姿字段用 SpinLock**，适合 50Hz 高频更新场景，减少系统调用开销。

### 2.5 心跳、看门狗与掉线检测

服务端和客户端都实现了“应用层健康检测”：

- **服务端**每 100ms 检查一次会话是否超时，超过配置值后把车辆标记为 `OFFLINE`。
- **客户端**每 0.2s 发一次心跳，并在收到任何服务器消息时刷新“最后收到服务端消息时间”。
- **客户端看门狗**超时后进入 `E_STOP`，用于模拟“服务器失联后的安全制动”。

这一层和 TCP 本身的连接状态不同：

- TCP 还没断，不代表业务一定活着。
- 所以项目明确使用**应用层心跳 + 会话状态机**来做更快、更符合业务语义的存活判断。

### 2.6 低电量自动充电逻辑

服务端收到 `AgvTelemetry` 后，会立即检查电量：

- 当 `battery < 20.0` 且当前状态不是 `CHARGING` 时，
- 服务端会立刻下发一个 `AgvCommand`，其类型是 `CMD_NAVIGATE_TO`，
- 客户端在低电量场景下会把这个导航命令解释为“去充电桩”。

客户端随后会经历这样一个状态流：

```text
IDLE -> MOVING_TO_CHARGER -> CHARGING
```

充电完成后客户端不会自动恢复，而是保持 `CHARGING` 状态，等待服务端发送 `RESUME`。

### 2.7 快慢分离：IO 线程与 Worker 线程池分工

项目把消息处理分为两类：

#### 直接在 IO 线程处理的消息

- `AgvTelemetry`
- `Heartbeat`
- `AgvCommand`
- `LatencyProbe` 的 Pong 处理

这些消息的特点是：

- 频率高
- 逻辑短
- 必须快

#### 投递到 Worker 线程池处理的消息

- `NavigationTask`

处理流程是：

1. IO 线程收到 `NavigationTask`。
2. 构造 `WorkerTask`，其中保存连接弱引用、会话强引用、消息对象和时间戳。
3. 投递到 `ThreadPool`。
4. Worker 线程模拟 200ms 数据库写入。
5. 处理完成后通过 `runInLoop` 回到 IO 线程发送 `CommonResponse`。

这样做的核心收益是：

- 慢任务不会卡住遥测和心跳。
- 任务执行过程如果连接断开，也能靠弱引用安全感知并取消后续发送。

### 2.8 紧急制动透传

`AgvCommand` 中的控制命令走的是“最快路径”。

服务器收到某客户端发来的控制指令后：

1. 直接根据 `target_agv_id` 查找目标会话。
2. 直接拿到目标连接。
3. **在 IO 线程内立即透传给目标车辆**。
4. 给源客户端回一个 `CommonResponse`。

这条链路**不进入 Worker 队列**，原因很明确：

- 紧急制动属于最高优先级控制包。
- 排队会引入额外不可控延迟。

### 2.9 RTT 延迟监控

服务端内置 `LatencyMonitor`：

- 周期性给所有在线车辆发 `LatencyProbe` Ping。
- 每个 Ping 带序列号和发送时间戳。
- 客户端收到后原样回 Pong。
- 服务端收到 Pong 后计算 RTT，并维护：
  - 最近 RTT
  - 平均 RTT
  - 最小 RTT
  - 最大 RTT
  - 样本数

同时，`LatencyMonitor` 会清理长时间未回复的 pending 探测项，避免内存泄漏。

### 2.10 模拟客户端不是“假发包器”，而是状态机客户端

`MockAgvClient` 不只是一个把 protobuf 发出去的小工具，它有自己的简化车辆状态机：

- `IDLE`
- `MOVING`
- `E_STOP`
- `MOVING_TO_CHARGER`
- `CHARGING`

它还实现了：

- 自动发遥测
- 自动发心跳
- 电量按状态变化
- 掉线急停
- 收到充电命令后去充电桩
- 收到 `LatencyProbe` 后自动回复 Pong

所以它既能做功能联调，也能做轻量压测和系统行为验证。

### 2.11 综合测试覆盖面

项目目前主测试入口是 `bin/test_lsk_server`，总计 32 个用例，覆盖：

- Buffer 整数操作
- Codec 编解码
- Protobuf 序列化
- ConcurrentMap 并发读写
- SpinLock 并发读写
- ProtobufDispatcher 分发
- SessionManager 会话 CRUD
- LatencyMonitor 统计
- 单客户端遥测
- 心跳保活
- 低电量自动充电
- 多客户端并发
- 会话超时标记
- Worker 与 IO 隔离
- 紧急制动透传
- RTT 监控
- 200ms 阻塞注入
- 断连安全性
- MockAgvClient 状态机联调

---

## 3. 运行环境与依赖

推荐环境：

- Linux
- GCC / G++ 9+
- CMake 3.10+
- Protobuf 3
- pthread

脚本会自动检查或安装的常见依赖包括：

- `build-essential`
- `cmake`
- `make`
- `libprotobuf-dev`
- `protobuf-compiler`

---

## 4. 编译、增量编译、重编译详细步骤

本项目已经提供了完整的 `build.sh`，一般优先用它。

### 4.1 一键编译脚本用法

第一次使用建议先赋予执行权限：

```bash
cd /home/ubuntu2004/server/lsk_muduo
chmod +x build.sh cleanup.sh run_full_test.sh
```

#### 常用编译命令

```bash
./build.sh
```

含义：

- 默认使用 **Release** 模式。
- 自动检测依赖。
- 自动创建 `build/`、`bin/`、`lib/`。
- 自动执行 `cmake ..` 和 `make -j$(nproc)`。

#### Debug 编译

```bash
./build.sh -d
```

等价于把构建类型切换到 `Debug`，适合断点调试、配合 `gdb` 使用。

#### 完全重编译

```bash
./build.sh -r
```

含义：

- 先清理 `build/`、`bin/`、`lib/` 产物。
- 再重新配置和编译。
- 适合协议、CMake 配置或依赖切换后使用。

#### 只清理，不编译

```bash
./build.sh -c
```

含义：

- 删除构建目录。
- 清空 `bin/` 下可执行文件。
- 清理 `lib/` 下库文件。

#### 指定并行编译任务数

```bash
./build.sh -j 8
```

适合手动控制编译并发度；如果不写，脚本默认使用 `nproc` 返回的 CPU 核心数。

#### 跳过依赖检测

```bash
./build.sh --skip-deps
```

适合你已经确认编译环境齐全，只想直接构建时使用。

### 4.2 build.sh 参数总表

| 参数 | 作用 | 典型场景 |
|---|---|---|
| `-h`, `--help` | 显示帮助 | 查看脚本说明 |
| `-d`, `--debug` | 使用 Debug 模式 | 调试源码 |
| `-r`, `--rebuild` | 清理后重编 | 大改后全量编译 |
| `-c`, `--clean` | 仅清理 | 删除旧产物 |
| `-j N` | 指定编译并发数 | 控制 CPU 占用 |
| `--skip-deps` | 跳过依赖检测与安装 | 环境已准备好 |

### 4.3 手动 CMake 编译步骤

如果你不想使用脚本，可以手动编译：

#### Release 模式

```bash
cd /home/ubuntu2004/server/lsk_muduo
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
make -j$(nproc)
```

#### Debug 模式

```bash
cd /home/ubuntu2004/server/lsk_muduo
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
make -j$(nproc)
```

### 4.4 重新配置时需要注意的点

`build.sh` 内部会检查：

- 当前源码根目录是否和旧的 `CMakeCache.txt` 一致。
- 当前 `CMAKE_BUILD_TYPE` 是否和缓存一致。

如果不一致，脚本会自动清掉旧缓存，避免“路径变了、用户变了、模式切换了但缓存没清”的常见问题。

### 4.5 编译产物说明

编译成功后，主要产物在这里：

- `bin/gateway_main`：服务端程序。
- `bin/agv_client_main`：模拟客户端程序。
- `bin/test_lsk_server`：综合测试程序。
- `lib/*.a`：静态库产物。
- `build/compile_commands.json`：可供 clangd / VS Code 索引使用。

---

## 5. 启动服务器和客户端的详细步骤

下面给出最实用的一套本地联调流程。

### 5.1 第一步：可选清理旧进程

如果你之前跑过服务端或客户端，先执行：

```bash
cd /home/ubuntu2004/server/lsk_muduo
./cleanup.sh
```

这个脚本会：

- 强制清理残留的 `gateway_main` 进程。
- 强制清理残留的 `agv_client_main` 进程。
- 等待端口释放。
- 检查默认端口 `8000` 是否空闲。

### 5.2 第二步：编译项目

```bash
cd /home/ubuntu2004/server/lsk_muduo
./build.sh
```

如果你要调试，换成：

```bash
./build.sh -d
```

### 5.3 第三步：启动服务器

最基础的启动方式：

```bash
./bin/gateway_main
```

这时服务器默认配置是：

- `--port 8000`
- `--timeout 5.0`
- `--threads 0`

另外，`gateway_main` 当前只暴露 **IO 线程数** 参数；`GatewayServer` 内部的 **Worker 线程池默认是 4 线程**，用于处理 `NavigationTask` 这类慢业务。

也就是：

- 监听 `8000` 端口。
- 会话超时阈值 5 秒。
- 单 Reactor 模式。

#### 常见服务端启动命令

```bash
./bin/gateway_main --port 8000 --timeout 5.0 --threads 0
./bin/gateway_main --port 8000 --timeout 5.0 --threads 4
./bin/gateway_main --port 9000 --timeout 3.0 --threads 2
```

### 5.4 第四步：启动一个客户端

最基础的启动方式：

```bash
./bin/agv_client_main
```

客户端默认配置是：

- `--id AGV-DEFAULT`
- `--server 127.0.0.1:8000`
- `--freq 10.0`
- `--battery 100.0`
- `--timeout 8.0`

这意味着默认客户端会：

- 连接本机 `8000` 端口。
- 以 10Hz 发遥测。
- 初始满电。
- 服务端消息超时 8 秒后进入看门狗急停。

#### 常见客户端启动命令

```bash
./bin/agv_client_main --id AGV-001 --server 127.0.0.1:8000
./bin/agv_client_main --id AGV-002 --server 127.0.0.1:8000 --freq 50
./bin/agv_client_main --id AGV-003 --server 127.0.0.1:8000 --battery 18
./bin/agv_client_main --id AGV-004 --server 127.0.0.1:8000 --freq 100 --timeout 3.0
```

### 5.5 第五步：观察典型联调场景

#### 场景 A：单车正常上报

服务端终端：

```bash
./bin/gateway_main --port 8000 --timeout 5.0 --threads 0
```

客户端终端：

```bash
./bin/agv_client_main --id AGV-NORMAL --server 127.0.0.1:8000 --freq 10 --battery 80
```

你会看到：

- 客户端连上服务器。
- 客户端开始持续发遥测和心跳。
- 服务端创建对应 `AgvSession`。

#### 场景 B：低电量自动充电

服务端终端：

```bash
./bin/gateway_main --port 8000 --timeout 5.0 --threads 0
```

客户端终端：

```bash
./bin/agv_client_main --id AGV-LOWBAT --server 127.0.0.1:8000 --battery 18
```

预期现象：

- 客户端上报低电量遥测。
- 服务端检测到电量低于 20%。
- 服务端下发 `CMD_NAVIGATE_TO`。
- 客户端把它解释为“去充电桩”，状态切换到 `MOVING_TO_CHARGER` / `CHARGING`。

#### 场景 C：多客户端并发联调

先开服务器：

```bash
./bin/gateway_main --port 8000 --timeout 5.0 --threads 4
```

再分别开多个客户端：

```bash
./bin/agv_client_main --id AGV-001 --server 127.0.0.1:8000 --freq 10
./bin/agv_client_main --id AGV-002 --server 127.0.0.1:8000 --freq 10
./bin/agv_client_main --id AGV-003 --server 127.0.0.1:8000 --freq 50
./bin/agv_client_main --id AGV-004 --server 127.0.0.1:8000 --battery 25
```

预期现象：

- 服务端可同时维护多个会话。
- 每个会话按自己的 `agv_id` 单独管理。
- 多个客户端同时发消息时，服务端仍能正常处理。

#### 场景 D：高频压测式运行

```bash
./bin/gateway_main --threads 4
./bin/agv_client_main --id AGV-STRESS --server 127.0.0.1:8000 --freq 100 --battery 40
```

这个场景用于快速观察高频遥测对服务器的压力。

### 5.6 服务端参数说明

| 参数 | 默认值 | 说明 | 建议 |
|---|---:|---|---|
| `--port` | `8000` | 监听端口 | 本地联调保持 8000 最方便 |
| `--timeout` | `5.0` | 会话超时秒数 | 联调可用 5 秒，快速实验可调小 |
| `--threads` | `0` | IO 线程数，0 表示单 Reactor | 压测或多客户端时建议设为 2 或 4 |

#### 服务端配置建议

- **单机调试**：`--threads 0`
- **多客户端联调**：`--threads 4`
- **快速超时测试**：`--timeout 1.0`
- **改端口运行**：同步修改客户端 `--server ip:port`

### 5.7 客户端参数说明

| 参数 | 默认值 | 说明 | 建议 |
|---|---:|---|---|
| `--id` | `AGV-DEFAULT` | 车辆 ID | 多车测试时必须保证唯一 |
| `--server` | `127.0.0.1:8000` | 服务器地址 | 改端口时必须同步修改 |
| `--freq` | `10.0` | 遥测频率 Hz | 功能联调用 10，压力测试可用 50 或 100 |
| `--battery` | `100.0` | 初始电量 | 测充电逻辑可直接设为 18 |
| `--timeout` | `8.0` | 客户端下行看门狗超时秒数 | 模拟失联可调成 1~3 秒 |

### 5.8 停止服务端和客户端

#### 优雅退出

- 服务端支持 `Ctrl + C`，内部捕获 `SIGINT` / `SIGTERM` 后会退出事件循环。
- 客户端直接 `Ctrl + C` 即可结束进程。

#### 强制清理残留进程

```bash
./cleanup.sh
```

---

## 6. 测试与验证命令

### 6.1 运行综合测试

```bash
./bin/test_lsk_server
```

当前综合测试覆盖以下测试组：

- `BufferTest`
- `CodecTest`
- `ProtobufTest`
- `ConcurrentMapTest`
- `SpinLockTest`
- `DispatcherTest`
- `SessionManagerTest`
- `LatencyMonitorTest`
- `IntegrationTest`

### 6.2 按组运行测试

```bash
./bin/test_lsk_server --gtest_filter=IntegrationTest.*
./bin/test_lsk_server --gtest_filter=LatencyMonitorTest.*
./bin/test_lsk_server --gtest_filter=SpinLockTest.*
```

### 6.3 查看测试清单

```bash
./bin/test_lsk_server --gtest_list_tests
```

### 6.4 运行完整性能脚本

```bash
./run_full_test.sh
```

这个脚本会自动执行多轮测试，包括：

- 单 Reactor + 10Hz
- 单 Reactor + 50Hz
- 多 Reactor + 10Hz
- 多 Reactor + 50Hz
- 多 Reactor + 100Hz
- 低电量触发充电流程

脚本会把日志写入：

- `/tmp/test_server.log`
- `/tmp/test_client.log`

并统计：

- 看门狗超时次数
- ERROR 数量
- 电量变化次数

---

## 7. 典型工作流总结

### 7.1 最常用的日常流程

```bash
cd /home/ubuntu2004/server/lsk_muduo
./cleanup.sh
./build.sh
./bin/gateway_main --threads 4
./bin/agv_client_main --id AGV-001 --server 127.0.0.1:8000 --freq 10
```

### 7.2 调试低电量逻辑

```bash
./bin/gateway_main
./bin/agv_client_main --id AGV-LOW --server 127.0.0.1:8000 --battery 18
```

### 7.3 调试高频遥测与多 Reactor

```bash
./bin/gateway_main --threads 4
./bin/agv_client_main --id AGV-FAST --server 127.0.0.1:8000 --freq 100 --battery 40
```

### 7.4 调试会话超时

```bash
./bin/gateway_main --timeout 1.0
./bin/agv_client_main --id AGV-TIMEOUT --server 127.0.0.1:8000 --freq 10
```

说明：服务端会更快把会话标记为 `OFFLINE`。

---

## 8. 项目结构总览（带文件说明）

下面这份结构图尽量覆盖当前仓库中最重要的源码、脚本和构建产物；每个条目后都附一条作用说明，方便你面试、汇报或二次开发时快速定位。

```text
lsk_muduo/
├── .gitignore                           # Git 忽略规则，避免把构建目录、产物和临时文件提交进版本库。
├── CMakeLists.txt                       # 顶层 CMake 入口，负责设置 C++17、输出目录、依赖查找和子模块编译顺序。
├── README.md                            # 项目总说明文档，介绍背景、构建方法、运行步骤、结构和设计要点。
├── build.sh                             # 一键编译脚本，负责依赖检查、配置 CMake、并行编译、重编译和清理。
├── cleanup.sh                           # 运行环境清理脚本，用于杀掉残留服务端和客户端进程并检查 8000 端口是否释放。
├── run_full_test.sh                     # 自动化性能验证脚本，会按不同线程数和频率组合批量启动服务端与客户端测试。
├── agv_server/                          # AGV 网关服务端源码目录，包含入口、协议、编解码和核心业务逻辑。
│   ├── CMakeLists.txt                   # 服务端子模块构建入口，负责组装 proto、codec、gateway 并生成 gateway_main。
│   ├── GatewayMain.cc                   # 服务端 main 函数所在文件，负责解析参数、创建 EventLoop、启动 GatewayServer 和处理退出信号。
│   ├── codec/                           # 协议编解码层目录，负责把 Protobuf 消息封装成长度头帧并解包。
│   │   ├── CMakeLists.txt               # 编解码模块的构建文件，生成 agv_codec 静态库。
│   │   ├── LengthHeaderCodec.h          # 编解码器声明文件，定义 8 字节头部格式、消息长度限制和编码解码接口。
│   │   └── LengthHeaderCodec.cc         # 编解码器实现文件，完成粘包半包处理、消息长度检查和序列化帧组装。
│   ├── gateway/                         # 网关核心逻辑目录，承载会话管理、消息分发、定时任务和业务处理。
│   │   ├── AgvSession.h                 # AGV 会话类声明文件，封装车辆连接、状态、电量、活跃时间和位姿数据。
│   │   ├── AgvSession.cc                # AGV 会话类实现文件，提供初始状态、默认电量和默认位姿设置。
│   │   ├── CMakeLists.txt               # 网关核心模块构建文件，生成 agv_gateway 静态库。
│   │   ├── ConcurrentMap.h              # 线程安全哈希表模板，使用 shared_mutex 支持并发读写和批量遍历。
│   │   ├── GatewayServer.h              # 网关服务类声明文件，定义连接回调、消息处理、定时器逻辑和发送接口。
│   │   ├── GatewayServer.cc             # 网关服务类实现文件，真正串起 TcpServer、Dispatcher、SessionManager、ThreadPool 和 LatencyMonitor。
│   │   ├── LatencyMonitor.h             # RTT 监控器声明文件，定义 Ping/Pong 记录结构和延迟统计接口。
│   │   ├── LatencyMonitor.cc            # RTT 监控器实现文件，负责创建探针、计算 RTT、更新均值并清理超时项。
│   │   ├── ProtobufDispatcher.h         # Protobuf 模板分发器，按消息类型把字节流解析成强类型消息再调用对应 handler。
│   │   ├── SessionManager.h             # 会话管理器声明文件，对 ConcurrentMap 做领域化封装并提供按连接删除会话等能力。
│   │   ├── SessionManager.cc            # 会话管理器实现文件，负责注册、查找、移除、遍历和获取 AGV ID 快照。
│   │   └── WorkerTask.h                 # Worker 任务结构定义文件，用于跨线程携带连接弱引用、消息对象和排队时间信息。
│   └── proto/                           # 协议定义目录，统一描述所有上下行消息和枚举。
│       ├── CMakeLists.txt               # Protobuf 构建文件，调用 protobuf_generate_cpp 并生成 agv_proto 静态库。
│       ├── common.proto                 # 公共枚举与基础消息定义文件，包含状态码、指令类型、点位等基础协议元素。
│       ├── common.pb.cc                 # 由 protoc 生成的 common 协议 C++ 实现文件，通常不手动修改。
│       ├── common.pb.h                  # 由 protoc 生成的 common 协议 C++ 头文件，供业务代码直接使用。
│       ├── message.proto                # 业务消息定义文件，声明遥测、心跳、导航任务、控制指令和延迟探针等消息。
│       ├── message.pb.cc                # 由 protoc 生成的 message 协议 C++ 实现文件，提供序列化与反序列化能力。
│       ├── message.pb.h                 # 由 protoc 生成的 message 协议 C++ 头文件，供服务端和客户端共同引用。
│       └── message_id.h                 # 消息类型 ID 常量和辅助函数文件，负责把包头 MsgType 与具体业务消息对应起来。
├── test_muduo/                          # 测试与模拟客户端目录，既包含测试程序也包含手动联调客户端。
│   ├── AgvClientMain.cc                 # 模拟客户端 main 函数文件，负责解析命令行参数并启动 MockAgvClient。
│   ├── CMakeLists.txt                   # 测试模块构建文件，生成 agv_client_main 和 test_lsk_server 可执行文件。
│   ├── MockAgvClient.h                  # 模拟客户端类声明文件，定义车辆状态机、定时器、指令处理和看门狗逻辑。
│   ├── MockAgvClient.cc                 # 模拟客户端实现文件，负责发遥测、发心跳、回 Pong、耗电、充电和状态切换。
│   └── test_lsk_server.cc               # 综合测试源码，覆盖 Buffer、Codec、协议、会话、快慢分离、透传和延迟监控等功能。
├── muduo/                               # 自研精简版 muduo 网络库目录，是整个项目的底层通信基础设施。
│   ├── base/                            # 基础设施层目录，负责日志、时间、线程、自旋锁和计算线程池。
│   │   ├── AsyncLogging.cc              # 异步日志实现文件，负责后台线程批量刷盘与缓冲切换。
│   │   ├── AsyncLogging.h               # 异步日志声明文件，定义前后台缓冲协作和日志线程接口。
│   │   ├── CurrentThread.cc             # 当前线程辅助实现文件，缓存线程 ID 等线程局部信息。
│   │   ├── CurrentThread.h              # 当前线程辅助声明文件，提供获取线程 ID 的轻量接口。
│   │   ├── LogFile.cc                   # 日志文件实现文件，负责落盘、flush 和滚动写日志。
│   │   ├── LogFile.h                    # 日志文件声明文件，抽象单个日志文件的管理逻辑。
│   │   ├── LogStream.cc                 # 日志流实现文件，处理格式化输出和流式拼接。
│   │   ├── LogStream.h                  # 日志流声明文件，定义各种基础类型到日志缓冲的写入方式。
│   │   ├── Logger.cc                    # 日志门面实现文件，提供日志级别、时间戳和输出入口。
│   │   ├── Logger.h                     # 日志门面声明文件，定义 `LOG_INFO`、`LOG_WARN`、`LOG_ERROR` 等接口。
│   │   ├── SpinLock.h                   # 自旋锁实现文件，基于 TTAS 算法优化极短临界区的加锁开销。
│   │   ├── Thread.cc                    # 线程封装实现文件，负责启动线程、同步线程 ID 和 join 生命周期。
│   │   ├── Thread.h                     # 线程封装声明文件，把 `std::thread` 包装成更易用的工程组件。
│   │   ├── ThreadPool.cc                # 通用计算线程池实现文件，支持任务队列、条件变量唤醒和优雅停止。
│   │   ├── ThreadPool.h                 # 通用计算线程池声明文件，供网关 Worker 线程池直接使用。
│   │   ├── Timestamp.cc                 # 时间戳实现文件，负责当前时间获取和时间格式化。
│   │   ├── Timestamp.h                  # 时间戳声明文件，定义微秒级时间表示和常用工具接口。
│   │   ├── copyable.h                   # 可拷贝标记基类，用于表达“这个类型允许按值拷贝”。
│   │   └── noncopyable.h                # 禁拷贝基类，用于禁止网络核心对象被意外复制。
│   └── net/                             # 网络层目录，负责 Reactor、连接管理、定时器和缓冲区。
│       ├── Acceptor.cc                  # 监听器实现文件，负责 accept 新连接并交给 TcpServer。
│       ├── Acceptor.h                   # 监听器声明文件，封装监听 socket 和新连接回调。
│       ├── Buffer.cc                    # 缓冲区实现文件，负责 `readv/write`、扩容和字节数据搬移。
│       ├── Buffer.h                     # 缓冲区声明文件，提供 prepend、整数读写、字符串读取和读写指针管理。
│       ├── Callbacks.h                  # 回调类型定义文件，统一连接回调、消息回调和写完成回调签名。
│       ├── Channel.cc                   # Channel 实现文件，负责事件分发、回调触发和 tie 生命周期保护。
│       ├── Channel.h                    # Channel 声明文件，表示“某个 fd 对哪些事件感兴趣”的封装对象。
│       ├── Connector.cc                 # 主动连接器实现文件，负责非阻塞 connect、重试和连接建立阶段管理。
│       ├── Connector.h                  # 主动连接器声明文件，供 TcpClient 发起连接和自动重连使用。
│       ├── DefaultPoller.cc             # 默认 Poller 选择实现文件，决定当前平台使用哪种具体 Poller。
│       ├── EPollPoller.cc               # epoll Poller 实现文件，负责 `epoll_wait` 和 channel 注册更新。
│       ├── EPollPoller.h                # epoll Poller 声明文件，是 Linux 下的核心多路复用实现。
│       ├── EventLoop.cc                 # 事件循环实现文件，负责 poll、事件分发、wakeup 和 pending functor 执行。
│       ├── EventLoop.h                  # 事件循环声明文件，暴露 runInLoop、queueInLoop、runAfter、runEvery 等接口。
│       ├── EventLoopThread.cc           # 单个 IO 线程封装实现文件，在线程中创建并运行一个 EventLoop。
│       ├── EventLoopThread.h            # 单个 IO 线程封装声明文件，负责启动线程并返回对应 loop 指针。
│       ├── EventLoopThreadPool.cc       # IO 线程池实现文件，负责创建多个 sub-reactor 并轮询分配连接。
│       ├── EventLoopThreadPool.h        # IO 线程池声明文件，是多 Reactor 模式的基础组件。
│       ├── InetAddress.cc               # 地址封装实现文件，负责 IP/端口与 `sockaddr_in` 间转换。
│       ├── InetAddress.h                # 地址封装声明文件，为服务端监听和客户端连接统一表示网络地址。
│       ├── Poller.cc                    # Poller 抽象基类实现文件，定义多路复用器公共行为。
│       ├── Poller.h                     # Poller 抽象基类声明文件，为 epoll 等实现提供统一接口。
│       ├── Socket.cc                    # socket 封装实现文件，负责 bind、listen、accept、shutdown 和选项设置。
│       ├── Socket.h                     # socket 封装声明文件，把原始 fd 操作收束成对象化接口。
│       ├── TcpClient.cc                 # TCP 客户端实现文件，负责和 Connector 配合建立连接与回收连接。
│       ├── TcpClient.h                  # TCP 客户端声明文件，是 MockAgvClient 的底层主动连接组件。
│       ├── TcpConnection.cc             # TCP 连接实现文件，负责读写缓冲区、发送、半关闭和回调触发。
│       ├── TcpConnection.h              # TCP 连接声明文件，封装单条已建立连接的生命周期和 IO 事件。
│       ├── TcpServer.cc                 # TCP 服务器实现文件，负责监听、创建 TcpConnection、分配 IO 线程和管理连接表。
│       ├── TcpServer.h                  # TCP 服务器声明文件，是 GatewayServer 使用的底层服务端抽象。
│       ├── Timer.cc                     # 定时器对象实现文件，保存到期时间、回调、重复间隔和重启逻辑。
│       ├── Timer.h                      # 定时器对象声明文件，是 TimerQueue 管理的最小时间单元。
│       ├── TimerId.h                    # 定时器 ID 文件，用于对外取消某个已注册定时器。
│       ├── TimerQueue.cc                # 定时器队列实现文件，基于 timerfd 和红黑树统一管理所有定时任务。
│       ├── TimerQueue.h                 # 定时器队列声明文件，提供添加、取消、过期处理和重置定时器的能力。
│       └── WeakCallback.h               # 弱回调工具文件，用 weak_ptr 防止延迟回调执行时访问已析构对象。
├── bin/                                 # 可执行文件输出目录，通常保存编译后的运行入口程序。
│   ├── agv_client_main                  # 模拟客户端可执行文件，用于手动联调和轻量压力验证。
│   ├── gateway_main                     # 服务端可执行文件，用于启动 AGV 网关服务器。
│   └── test_lsk_server                  # 综合测试可执行文件，用于运行单元测试和集成测试。
├── lib/                                 # 静态库输出目录，保存编译生成的底层库和业务库产物。
├── build/                               # CMake 构建目录，保存缓存、Makefile、中间文件和生成的编译数据库。
│   └── compile_commands.json            # 编译命令数据库，供 clangd、IDE 索引和静态分析使用。
└── logs/                                # 运行日志目录，可存放多轮联调或压测产生的日志数据。
```

> 说明：`build/`、`bin/`、`lib/`、`logs/` 都属于运行或构建产物目录，核心阅读重点仍然是 `agv_server/`、`test_muduo/` 和 `muduo/`。

---

## 9. 这套项目最值得关注的设计点

如果你后续要继续写文档、准备答辩或者面试，这个仓库最值得强调的点有五个：

1. **网络库和业务层是一起做的**，不是简单调用第三方库写点业务代码。  
2. **协议、会话、编解码、客户端模拟器、服务端、测试**全部形成闭环。  
3. **快慢分离做得很明确**，`NavigationTask` 走 Worker，遥测和心跳留在 IO 线程。  
4. **紧急制动单独走透传通道**，这非常符合高优先级控制指令的工程需求。  
5. **测试是按系统能力组织的**，不是零散的演示代码，而是能证明整个架构行为是否成立。  

---

## 10. 当前可直接使用的三个入口程序

### 服务端

```bash
./bin/gateway_main --port 8000 --timeout 5.0 --threads 4
```

### 模拟客户端

```bash
./bin/agv_client_main --id AGV-001 --server 127.0.0.1:8000 --freq 10 --battery 80
```

### 综合测试

```bash
./bin/test_lsk_server
```

如果你只想记最关键的一组命令，记这三条就够了。

