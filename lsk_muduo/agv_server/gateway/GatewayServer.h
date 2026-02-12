#ifndef LSK_MUDUO_GATEWAY_SERVER_H
#define LSK_MUDUO_GATEWAY_SERVER_H

#include "../../muduo/net/TcpServer.h"
#include "../../muduo/net/EventLoop.h"
#include "../../muduo/net/Buffer.h"
#include "../../muduo/base/ThreadPool.h"
#include "AgvSession.h"
#include "SessionManager.h"
#include "ProtobufDispatcher.h"
#include "WorkerTask.h"
#include "LatencyMonitor.h"
#include "../proto/message.pb.h"
#include "../proto/common.pb.h"
#include "../proto/message_id.h"
#include "../codec/LengthHeaderCodec.h"
#include <memory>
#include <functional>

namespace agv {
namespace gateway {

/**
 * @brief AGV 网关服务器（迭代二升级版）
 * 
 * @note 核心职责：
 *       1. 管理车辆连接会话（AgvSession）
 *       2. 上行看门狗：100ms 周期检查，1s 超时报警
 *       3. 基础业务引擎：低电量（< 20%）自动触发充电
 *       4. 协议解析：LengthHeader(8字节) + Protobuf
 * 
 * @note 迭代二重构：
 *       - 消息分发：ProtobufDispatcher 模板化类型安全分发（替换 switch-case）
 *       - 会话管理：SessionManager 封装会话生命周期管理
 *       - AgvSession：持有连接弱引用（weak_ptr<TcpConnection>），符合文档要求
 * 
 * @note 架构设计（迭代三升级）：
 *       - Reactor + Worker 模式：IO 线程处理高频消息，Worker 线程处理耗时业务
 *       - IO 线程：Telemetry/Heartbeat 直接处理（更新内存）
 *       - Worker 线程：NavigationTask（数据库操作）投递到 ThreadPool
 */
class GatewayServer {
public:
    // ==================== 构造与生命周期 ====================
    
    /**
     * @brief 构造函数
     * @param loop 事件循环（必须在 IO 线程创建）
     * @param listen_addr 监听地址（端口）
     * @param name 服务器名称（用于日志）
     * @param session_timeout_sec 会话超时时间（秒），默认 5.0s（迭代二要求）
     * @param worker_threads Worker 线程数，默认 4（迭代三新增）
     */
    GatewayServer(lsk_muduo::EventLoop* loop,
                  const lsk_muduo::InetAddress& listen_addr,
                  const std::string& name,
                  double session_timeout_sec = 5.0,
                  int worker_threads = 4);
    
    ~GatewayServer();

    /**
     * @brief 启动服务器
     * @note 启动后会：
     *       1. 调用 TcpServer::start() 开始监听
     *       2. 启动看门狗定时器（100ms 周期）
     */
    void start();

    /**
     * @brief 设置 IO 线程数（迭代三使用）
     * @param num_threads 线程数，0 表示单 Reactor
     */
    void setThreadNum(int num_threads) {
        server_.setThreadNum(num_threads);
    }

    /**
     * @brief 设置延迟探测间隔（必须在 start() 前调用）
     * @param seconds 探测间隔（秒），默认 5.0
     */
    void setLatencyProbeInterval(double seconds) {
        latency_probe_interval_sec_ = seconds;
    }

    /**
     * @brief 获取延迟监控器（只读，用于查询 RTT 统计）
     */
    const LatencyMonitor& getLatencyMonitor() const { return latency_monitor_; }

    /**
     * @brief 获取会话管理器（只读，用于测试验证）
     */
    const SessionManager& getSessionManager() const { return sessionManager_; }

private:
    // ==================== TcpServer 回调 ====================
    
    /**
     * @brief 连接建立/断开回调
     * @note IO 线程调用
     * @note 连接断开时，遍历 connections_ 清理对应会话
     */
    void onConnection(const lsk_muduo::TcpConnectionPtr& conn);

    /**
     * @brief 消息到达回调
     * @note IO 线程调用
     * @note 核心流程：
     *       1. 循环解析包头（处理粘包）
     *       2. 检查负载完整性（处理半包）
     *       3. 根据 MsgType 分发到不同处理函数
     */
    void onMessage(const lsk_muduo::TcpConnectionPtr& conn,
                   lsk_muduo::Buffer* buf,
                   lsk_muduo::Timestamp receive_time);

    // ==================== 消息分发（ProtobufDispatcher）====================
    
    /**
     * @brief 初始化消息分发器
     * @note 在构造函数中调用，注册所有消息类型的处理函数
     * @note 新增消息类型只需在此函数中添加一行 registerHandler 调用
     */
    void initDispatcher();

    // ==================== 业务处理（按消息类型分发）====================
    
    /**
     * @brief 处理遥测数据（MSG_AGV_TELEMETRY）
     * @note 高频消息（50Hz）
     * @note 逻辑：
     *       1. 更新 Session（电量、位姿、活跃时间）
     *       2. 触发业务引擎（检查低电量）
     */
    void handleTelemetry(const lsk_muduo::TcpConnectionPtr& conn,
                         const proto::AgvTelemetry& msg);

    /**
     * @brief 处理心跳消息（MSG_HEARTBEAT）
     * @note 中频消息（1Hz）
     * @note 逻辑：刷新 last_active_time
     */
    void handleHeartbeat(const lsk_muduo::TcpConnectionPtr& conn,
                         const proto::Heartbeat& msg);

    /**
     * @brief 处理导航任务（MSG_NAVIGATION_TASK）【迭代三新增】
     * @note 低频消息（事件驱动）
     * @note 逻辑：
     *       1. 构造 WorkerTask（弱引用连接、强引用会话、消息）
     *       2. 投递到 Worker 线程池（模拟数据库写入 50ms）
     *       3. Worker 完成后通过 runInLoop 回到 IO 线程发送响应
     */
    void handleNavigationTask(const lsk_muduo::TcpConnectionPtr& conn,
                              const proto::NavigationTask& msg);

    /**
     * @brief 处理系统指令（MSG_AGV_COMMAND）【迭代三第6周新增】
     * @note IO 线程直接处理（透传，不进 Worker 队列）
     * @note 核心流程：
     *       1. 查找目标车辆会话（target_agv_id）
     *       2. 直接通过目标连接转发 AgvCommand
     *       3. 记录处理延迟（用于后续统计）
     *       4. EMERGENCY_STOP 等高优先级指令绝不进队列
     */
    void handleAgvCommand(const lsk_muduo::TcpConnectionPtr& conn,
                          const proto::AgvCommand& cmd);

    /**
     * @brief 处理延迟探测响应（MSG_LATENCY_PROBE）【迭代三第6周新增】
     * @note IO 线程处理
     * @note 逻辑：收到 Pong（is_response=true）后交给 LatencyMonitor 计算 RTT
     */
    void handleLatencyProbe(const lsk_muduo::TcpConnectionPtr& conn,
                            const proto::LatencyProbe& probe);

    // ==================== 会话管理（SessionManager）====================
    
    /**
     * @brief 注册新会话
     * @note 首次收到该 agv_id 的消息时调用
     * @note 线程安全：SessionManager 内部持有读写锁
     */
    void registerSession(const std::string& agv_id,
                         const lsk_muduo::TcpConnectionPtr& conn);

    /**
     * @brief 移除会话
     * @note 连接断开或超时时调用
     * @note 线程安全：ConcurrentMap 内部持有读写锁
     */
    void removeSession(const std::string& agv_id);

    /**
     * @brief 查找会话
     * @return shared_ptr 拷贝（线程安全，即使其他线程 erase 也不会悬挂）
     */
    AgvSessionPtr findSession(const std::string& agv_id);

    /**
     * @brief 根据连接对象反查 agv_id 并移除会话
     * @note 连接断开时调用，使用 SessionManager::removeSessionByConnection
     */
    void removeSessionByConnection(const lsk_muduo::TcpConnectionPtr& conn);

    /**
     * @brief 上行看门狗定时器回调（100ms 周期）
     * @note Timer 线程调用
     * @note 逻辑：遍历所有 Session，检查 last_active_time
     *       若超过 1000ms，标记为 OFFLINE 并打印报警日志
     */
    void onWatchdogTimer();

    /**
     * @brief 延迟探测定时器回调【迭代三第6周新增】
     * @note 每 N 秒对所有在线连接发送 LatencyProbe Ping
     * @note 并输出当前 RTT 统计日志
     */
    void onLatencyTimer();

    // ==================== 基础业务引擎 ====================
    
    /**
     * @brief 检查低电量并触发充电
     * @note IO 线程调用（handleTelemetry 中）
     * @note 逻辑：if (battery < 20% && state != CHARGING) -> 下发充电指令
     */
    void checkLowBatteryAndCharge(const AgvSessionPtr& session,
                                  const lsk_muduo::TcpConnectionPtr& conn);

    // ==================== 消息发送 ====================
    
    /**
     * @brief 发送 Protobuf 消息（通用接口）
     * @param conn 连接对象
     * @param msg_type 消息类型（填入包头）
     * @param message Protobuf 消息对象
     * 
     * @note 封装流程：Protobuf 序列化 -> 构造 LengthHeader -> 发送
     */
    void sendProtobufMessage(const lsk_muduo::TcpConnectionPtr& conn,
                             uint16_t msg_type,
                             const google::protobuf::Message& message);

    /**
     * @brief 下发充电指令
     * @note 发送 AgvCommand（CMD_NAVIGATE_TO）指示车辆导航到充电桩
     */
    void sendChargeCommand(const std::string& agv_id,
                          const lsk_muduo::TcpConnectionPtr& conn);

    // ==================== Worker 线程任务处理【迭代三新增】====================
    
    /**
     * @brief Worker 线程任务处理函数
     * @param task 任务对象（包含连接、会话、消息、时间戳）
     * 
     * @note Worker 线程调用（非 IO 线程）
     * @note 核心流程：
     *       1. 检查连接有效性（task.getConnection()）
     *       2. 模拟数据库操作（usleep(50000) = 50ms）
     *       3. 通过 runInLoop 回到 IO 线程发送响应
     */
    void processWorkerTask(const std::shared_ptr<WorkerTask>& task);
    
    /**
     * @brief 模拟数据库写入操作
     * @param msg NavigationTask 消息
     * 
     * @note Worker 线程调用
     * @note 模拟场景：存储任务到 MySQL/InfluxDB（用于事故回溯和数字孪生）
     */
    void simulateDatabaseWrite(const proto::NavigationTask& msg);

private:
    // ==================== 成员变量 ====================
    
    lsk_muduo::EventLoop* loop_;                   ///< 事件循环（IO 线程）
    lsk_muduo::TcpServer server_;                  ///< TCP 服务器
    ProtobufDispatcher dispatcher_;                ///< 消息分发器（模板化类型安全）
    
    /// 会话管理器（封装会话 CRUD 操作，线程安全）
    SessionManager sessionManager_;
    
    /// 可配置的超时参数（毫秒）
    int session_timeout_ms_;
    
    /// Worker 线程池（迭代三新增，处理耗时业务）
    std::unique_ptr<ThreadPool> worker_pool_;

    /// 延迟监控器（迭代三第6周新增）
    LatencyMonitor latency_monitor_;

    /// 延迟探测间隔（秒），默认 5.0
    double latency_probe_interval_sec_ = 5.0;

    // ====================  常量配置 ====================
    
    static constexpr int kWatchdogIntervalMs = 100;   ///< 看门狗周期（毫秒）
    static constexpr double kLowBatteryThreshold = 20.0;  ///< 低电量阈值（%）
};

}  // namespace gateway
}  // namespace agv

#endif  // LSK_MUDUO_GATEWAY_SERVER_H
