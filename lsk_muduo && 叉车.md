##### 原叉车项目描述：
项目描述：（“十四五”重点项目）仓库分拣无人叉车自主导航系统。实现无人自动驾驶叉车在狭窄货架仓库
内的不同装卸平台间的货物运送。装载入库：无人叉车自动导航至“装载平台”叉取货物后精准导航至指定货
架并将货物填入货架货位；出库分拣：无人叉车导航至指定货架的货位取出货物送往指定的“分拣平台”，实
现3cm内的导航精度。
 项目难点：车辆需在仅2米宽的巷道（车辆宽1.67米，单侧物理间隙仅16.5厘米）内，以不低于1.67m/s的速度完
成高速巡航、紧急制动及高精度工位对接。系统需克服重载车辆在大惯性、高速度工况、高控制时延下显著的
非线性动力学特性，同时确保全流程运行轨迹与停靠精度的鲁棒性。
 负责内容（无人叉车自主导航框架构建）：
 3D点云地图压缩为2D栅格地图，将每个标记栅格为“空白”或“被占据”，用于后续叉车的路径规划。
 构建基于GRU的动力学残差模型，有效补偿基准运动学模型在高速大惯性工况下的预测偏差。
 设计MPPI控制架构，通过大规模粒子采样和多目标代价函数优化，实现复杂空间内高动态轨迹生成。
 融合神经网络预测与最优控制策略，解决大时延系统跟踪难题，实现全流程精度3cm以内的鲁棒控制。

##### Lsk_muduo结合叉车开发方案
将原瀑布式8周方案重构为4个迭代周期，每个周期结束时都有可运行、可验证的系统。压测模拟器设计为可配置的统一工具，支持从1台到1000台的任意规模测试。
核心调整策略
端到端优先：第2周末即可看到"客户端→服务器→回复"的完整链路
单元测试前置：每个模块完成后立即编写测试，不积压
压测器一次开发：通过命令行参数控制规模，避免重复开发
增量验证：每周都有明确的验收检查点
单元模块测试标准：都写在lsk_muduo/test_muduo文件夹下



##### 迭代一：最小可用系统（第1-2周）
目标：跑通"1个模拟客户端连接服务器，发送遥测数据，服务器接收处理并回复"的完整链路。

第1周：协议层与编解码
Day 1-2：Buffer增强
在 Buffer.h 中添加 peekInt32、readInt32、appendInt32、prependInt32 等整数操作方法（当前lsk_muduo缺失这些方法）
编写Buffer单元测试验证字节序转换和边界情况
Day 3-4：Protobuf协议定义
创建 agv_message.proto，仅定义最小必要消息：AgvTelemetry（上行遥测）、CommonResponse（下行响应）、Heartbeat（心跳）
其他消息如 AgvCommand、NavigatePayload、LatencyProbe 留待后续迭代添加
Day 5-7：LengthHeaderCodec编解码器
实现8字节包头（Length + MsgType + Flags）加Protobuf负载的编解码
编写单元测试验证粘包、拆包、半包场景

第2周：服务端与客户端骨架（双闭环安全版）
Day 1-2：GatewayServer 骨架（状态与看门狗）
目标：从“无状态回声”升级为“有状态监管”。
核心开发：
基于 TcpServer 实现GatewayServer
Session 管理：定义 AgvSession 类（包含 agv_id、last_active_time、battery_level）。在 GatewayServer 中维护 std::map<string, AgvSessionPtr>。
上行看门狗（Server Watchdog）：
集成 TimerQueue，每 100ms 运行一次检查函数。
逻辑：遍历 Session Map，若 now - last_active_time > 1000ms，标记该车为 OFFLINE 并打印[ALARM]日志。
基础业务引擎（Business Engine）：
在 onMessage 收到遥测数据后，不仅更新 Session，还进行逻辑判断。
逻辑：if (battery < 20.0 && !is_charging) -> 立即构造 AgvCommand (类型 MSG_NAVIGATION_TASK，目标 "CHARGER") 并下发。
Day 3-4：MockAgvClient 骨架（智能模拟器）
目标：从“只会发包的哑巴”升级为“能模拟物理特性的虚拟车”。
核心开发：
物理仿真模块：
维护内部变量 current_battery。启动定时器，每秒自动减少 0.5% 电量（模拟耗电）。
维护状态机：IDLE (空闲) -> MOVING (移动中) -> E_STOP (急停)。
下行看门狗（Client Watchdog）：
维护 last_server_msg_time。
逻辑：若 1秒 未收到服务器任何指令（包括心跳），自动切换状态为 E_STOP 并打印 [EMERGENCY] Server Lost!。
指令响应：
收到 AgvCommand 后，根据类型修改自身状态（如收到充电指令，状态变为 MOVING_TO_CHARGER）。
Day 5-7：端到端闭环联调
目标：验证“安全闭环”和“业务闭环”。
验证场景：
正常通信：Client 以 50Hz 发送 Telemetry，Server 收到并不回复（除非有指令），只在后台静默更新心跳。
安全测试（拔网线模拟）：
杀掉 Client 进程 -> Server 在 1秒内打印 [ALARM] AGV Offline。
杀掉 Server 进程 -> Client 在 1秒内打印 [EMERGENCY] Server Lost 并停止发送。
业务测试（低电量触发）：
观察 Client 电量自然下降。
当电量跌破 20% 时，Server 自动下发充电指令。
Client 收到指令，日志显示“Receiving Charge Command, Moving to Charger...”。
迭代一验收标准（更新后）：
双向心跳保活：断开任一端，另一端能在 1秒内检测到并报警。
状态驱动调度：低电量场景能自动触发 Server 下发指令，Client 正确响应指令。
协议一致性：所有通信均通过 LengthHeaderCodec 和 Protobuf 进行，无解析错误。



##### 迭代二：会话管理与消息分发（第3-4周）
目标：支持多客户端连接，实现类型安全的消息分发和线程安全的会话管理。
第3周：消息分发与会话容器
Day 1-2：ProtobufDispatcher消息分发器
实现模板化的消息类型到处理函数映射
替换GatewayServer中的硬编码消息处理逻辑
编写单元测试验证分发正确性
Day 3-4：ConcurrentMap线程安全容器
实现读写锁保护的哈希表
关键设计：find 方法返回 shared_ptr 拷贝而非原始指针
编写单元测试验证并发读写安全性
Day 5-7：AgvSession与SessionManager
定义AgvSession结构体（车辆ID、连接弱引用、位姿、状态）
实现AgvSessionManager管理车辆上下线
集成到GatewayServer，连接时注册会话，断开时清理
第4周：多客户端验证与心跳
Day 1-2：MockAgvClient参数化改造
支持通过命令行指定车辆ID、服务器地址、发送频率
这是压测模拟器的核心组件，设计时考虑复用
Day 3-4：HeartbeatManager心跳检测
使用 TimerQueue 实现Timer方案
收到任意消息刷新超时，5秒无消息断开连接
编写测试验证超时断连功能
Day 5-7：多客户端联调
手动启动5-10个MockAgvClient进程连接Server
验证会话注册、遥测处理、心跳超时、断连清理全流程
迭代二验收标准：
10个Client同时连接，各自50Hz发送Telemetry
Server正确管理10个会话，Dispatcher正确分发消息
主动断开Client后5秒内会话被清理
ConcurrentMap和Dispatcher的单元测试全部通过

迭代三：IO与业务分离（第5-6周）
目标：实现快慢分离架构，IO线程处理高频遥测，Worker线程处理复杂业务。
第5周：线程分离与Task投递
Day 1-2：扩展Protobuf协议
添加 AgvCommand（含EMERGENCY_STOP、NAVIGATE_TO等类型）
添加 NavigatePayload、LatencyProbe 等消息
Day 3-4：Task封装与投递机制
设计Task结构体（弱引用连接、强引用会话、消息、时间戳）
利用现有 ThreadPool 作为Worker线程池
实现IO线程到Worker线程的任务投递
Day 5-7：快慢分离实现
Telemetry/Heartbeat在IO线程直接处理（更新内存）
NavigateTo等复杂指令投递到Worker线程
Worker处理完成后通过 runInLoop 回到IO线程发送响应
AgvSession中位姿字段添加自旋锁保护并发访问
第6周：紧急制动与延迟监控
Day 1-2：透传型紧急制动
EMERGENCY_STOP指令在IO线程直接转发，不进队列
记录处理时间用于后续延迟统计
Day 3-4：LatencyMonitor延迟监控
实现RTT测量机制（服务端Ping→客户端Pong→计算往返时间）
每5秒对每个连接测量一次RTT
MockAgvClient添加LatencyProbe响应支持
Day 5-7：BusinessHandler业务处理器
实现模拟的路径规划处理（可用sleep模拟耗时）
验证Worker线程阻塞不影响IO线程的遥测处理
迭代三验收标准：
50个Client连接，50Hz Telemetry全部在IO线程处理
发送NavigateTo指令，在Worker线程处理后正确响应
Worker线程添加100ms sleep后，Telemetry处理不受影响
LatencyMonitor能测量并输出RTT数据
紧急制动指令延迟可测量

迭代四：压测验证与收尾（第7-8周）
目标：完成可配置的压测模拟器，进行全面性能测试，输出文档和面试材料。
第7周：压测模拟器与性能验证
Day 1-3：LoadTester压测主程序（一次开发，多规模复用）
命令行参数：--server、--clients（1-1000）、--freq（Hz）、--mode（uniform/jitter/burst）、--duration
创建多个EventLoopThread，每个管理约100个MockAgvClient
运行时统计发送成功数、失败数、连接断开数
设计要点：同一份代码支持从1台到1000台的任意规模测试
Day 4-5：Statistics服务端统计模块
统计QPS、AvgRTT、P99/P999 RTT、连接数、队列深度
每5秒输出一次统计日志
Day 6-7：渐进式压测验证
先用10台客户端验证工具正确性
逐步增加到100、500、1000台
本地资源不足时可将LoadTester部署到云服务器或朋友电脑
第8周：专项测试与文档
Day 1-2：专项测试
连接风暴测试：1秒内建立1000连接
Worker阻塞注入测试：验证快慢分离有效性
会话并发访问测试：使用ThreadSanitizer验证无数据竞争
Day 3-4：性能分析与优化
使用perf生成火焰图分析CPU热点
根据瓶颈针对性优化（如有必要）
验证异步日志未阻塞IO线程
Day 5-7：文档与面试准备
编写README（项目简介、架构设计、性能报告、设计决策）
整理面试话术（原方案十二个问题）
截图保存火焰图和性能数据
迭代四验收标准：
LoadTester支持1-1000台任意规模压测
1000连接稳定运行10分钟无异常
性能指标达到目标（根据实际硬件配置验收）
完整的README文档和性能报告

时间规划总表
周次	阶段	核心产出	可运行系统状态
1	迭代一	Buffer增强、Protobuf定义、Codec实现	编解码组件可独立测试
2	迭代一	GatewayServer骨架、MockAgvClient骨架	1对1完整链路跑通
3	迭代二	Dispatcher、ConcurrentMap、SessionManager	多会话管理就绪
4	迭代二	心跳检测、多客户端联调	10客户端稳定运行
5	迭代三	线程分离、Task投递、快慢分离	IO与Worker分离验证
6	迭代三	紧急制动、LatencyMonitor、BusinessHandler	50客户端+完整功能
7	迭代四	LoadTester、Statistics、渐进式压测	1000客户端压测通过
8	迭代四	专项测试、火焰图、文档、面试话术	项目完成，可面试

Further Considerations
压测资源方案：本地单机压测可能受限于CPU/内存，建议使用低配云服务器（如2核4G）运行LoadTester，通过公网连接你本地的Server，成本约几元/小时。
迭代节奏灵活性：每个迭代预留1-2天缓冲，若某周进度超前可提前开始下一迭代，若遇阻塞可调整范围。
单元测试框架选择：建议使用Google Test，与CMake集成方便，可在迭代一开始时配置好测试框架。
