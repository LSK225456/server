#ifndef LSK_MUDUO_GATEWAY_SERVER_H
#define LSK_MUDUO_GATEWAY_SERVER_H

#include "../../muduo/net/TcpServer.h"
#include "../../muduo/net/EventLoop.h"
#include "../../muduo/net/Buffer.h"
#include "AgvSession.h"
#include "ProtobufDispatcher.h"
#include "ConcurrentMap.h"
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
 *       - 会话管理：ConcurrentMap<string, AgvSession> 读写锁保护（替换 std::map + mutex）
 *       - 连接管理：ConcurrentMap<string, TcpConnection> 管理 agv_id 到连接的映射
 * 
 * @note 架构设计：
 *       - 单 Reactor 模式：IO 线程 == EventLoop 线程
 *       - 迭代三会改为 Reactor + Worker 线程池
 */
class GatewayServer {
public:
    // ==================== 构造与生命周期 ====================
    
    /**
     * @brief 构造函数
     * @param loop 事件循环（必须在 IO 线程创建）
     * @param listen_addr 监听地址（端口）
     * @param name 服务器名称（用于日志）
     */
    GatewayServer(lsk_muduo::EventLoop* loop,
                  const lsk_muduo::InetAddress& listen_addr,
                  const std::string& name);
    
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

    // ==================== 会话管理（ConcurrentMap）====================
    
    /**
     * @brief 注册新会话
     * @note 首次收到该 agv_id 的消息时调用
     * @note 线程安全：ConcurrentMap 内部持有读写锁
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
     * @note 连接断开时调用，使用 ConcurrentMap::eraseIf
     */
    void removeSessionByConnection(const lsk_muduo::TcpConnectionPtr& conn);

    // ==================== 上行看门狗 ====================
    
    /**
     * @brief 看门狗定时器回调（100ms 周期）
     * @note Timer 线程调用
     * @note 逻辑：遍历所有 Session，检查 last_active_time
     *       若超过 1000ms，标记为 OFFLINE 并打印报警日志
     */
    void onWatchdogTimer();

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
     * @note 简化实现：发送 AgvCommand（类型 EMERGENCY_STOP）
     * @note 迭代三完善：发送 NavigationTask（目标 "CHARGER"）
     */
    void sendChargeCommand(const std::string& agv_id,
                          const lsk_muduo::TcpConnectionPtr& conn);

private:
    // ==================== 成员变量 ====================
    
    lsk_muduo::EventLoop* loop_;                   ///< 事件循环（IO 线程）
    lsk_muduo::TcpServer server_;                  ///< TCP 服务器
    ProtobufDispatcher dispatcher_;                ///< 消息分发器（模板化类型安全）
    
    /// 会话容器：agv_id -> AgvSession（读写锁保护）
    ConcurrentMap<std::string, AgvSession> sessions_;
    /// 连接容器：agv_id -> TcpConnection（读写锁保护）
    ConcurrentMap<std::string, lsk_muduo::TcpConnection> connections_;

    // ==================== 常量配置 ====================
    
    static constexpr int kWatchdogIntervalMs = 100;   ///< 看门狗周期（毫秒）
    static constexpr int kSessionTimeoutMs = 1000;    ///< 会话超时时间（毫秒）
    static constexpr double kLowBatteryThreshold = 20.0;  ///< 低电量阈值（%）
};

}  // namespace gateway
}  // namespace agv

#endif  // LSK_MUDUO_GATEWAY_SERVER_H
