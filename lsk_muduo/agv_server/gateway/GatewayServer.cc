#include "GatewayServer.h"
#include "../../muduo/base/Logger.h"
#include "../codec/LengthHeaderCodec.h"

namespace agv {
namespace gateway {

using namespace lsk_muduo;
using namespace agv::codec;

// ==================== 辅助函数 ====================

/**
 * @brief 计算两个时间戳的差值（秒）
 * @return 秒数（double 类型）
 */
static inline double timeDifference(Timestamp high, Timestamp low) {
    int64_t diff = high.microSecondsSinceEpoch() - low.microSecondsSinceEpoch();
    return static_cast<double>(diff) / 1000000.0;
}

// ==================== 构造与生命周期 ====================

GatewayServer::GatewayServer(EventLoop* loop,
                             const InetAddress& listen_addr,
                             const std::string& name)
    : loop_(loop),
      server_(loop, listen_addr, name) {
    
    // 注册 TcpServer 回调
    server_.setConnectionCallback(
        std::bind(&GatewayServer::onConnection, this, std::placeholders::_1));
    server_.setMessageCallback(
        std::bind(&GatewayServer::onMessage, this,
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    
    LOG_INFO << "GatewayServer created: " << name;
}

GatewayServer::~GatewayServer() {
    LOG_INFO << "GatewayServer destroyed";
}

void GatewayServer::start() {
    LOG_INFO << "GatewayServer starting...";
    
    // 1. 启动 TcpServer
    server_.start();
    
    // 2. 启动上行看门狗定时器（100ms 周期）
    loop_->runEvery(kWatchdogIntervalMs / 1000.0,
                    std::bind(&GatewayServer::onWatchdogTimer, this));
    
    LOG_INFO << "GatewayServer started (watchdog: " << kWatchdogIntervalMs
             << "ms, timeout: " << kSessionTimeoutMs << "ms)";
}

// ==================== TcpServer 回调实现 ====================

void GatewayServer::onConnection(const TcpConnectionPtr& conn) {
    if (conn->connected()) {
        LOG_INFO << "New connection: " << conn->peerAddress().toIpPort();
        // 注意：此时尚未收到 agv_id，会话注册延迟到首个消息到达
    } else {
        LOG_INFO << "Connection closed: " << conn->peerAddress().toIpPort();
        
        // 遍历 connections_，找到对应的 agv_id 并清理会话
        MutexLockGuard lock(sessions_mutex_);
        for (auto it = connections_.begin(); it != connections_.end(); ) {
            if (it->second == conn) {
                std::string agv_id = it->first;
                LOG_WARN << "AGV [" << agv_id << "] connection lost, removing session";
                sessions_.erase(agv_id);
                it = connections_.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void GatewayServer::onMessage(const TcpConnectionPtr& conn,
                              Buffer* buf,
                              Timestamp receive_time) {
    (void)receive_time;  // 暂未使用
    
    // 循环处理粘包：一个 Buffer 可能包含多个完整消息
    while (LengthHeaderCodec::hasCompleteMessage(buf)) {
        uint16_t msg_type = 0;
        uint16_t flags = 0;
        std::string payload;
        
        // 使用 LengthHeaderCodec 解码
        if (!LengthHeaderCodec::decode(buf, &msg_type, &payload, &flags)) {
            LOG_ERROR << "Failed to decode message from " << conn->peerAddress().toIpPort();
            conn->shutdown();  // 关闭连接，防止恶意攻击
            return;
        }
        
        // 处理 Protobuf 消息
        handleProtobufMessage(conn, msg_type, payload.data(), payload.size());
    }
}

// ==================== Protobuf 消息处理 ====================

void GatewayServer::handleProtobufMessage(const TcpConnectionPtr& conn,
                                          uint16_t msg_type,
                                          const char* payload,
                                          size_t len) {
    // 根据消息类型分发（使用 message_id.h 中的常量）
    switch (msg_type) {
        case proto::MSG_AGV_TELEMETRY: {
            proto::AgvTelemetry msg;
            if (!msg.ParseFromArray(payload, static_cast<int>(len))) {
                LOG_ERROR << "Failed to parse AgvTelemetry";
                return;
            }
            handleTelemetry(conn, msg);
            break;
        }
        case proto::MSG_HEARTBEAT: {
            proto::Heartbeat msg;
            if (!msg.ParseFromArray(payload, static_cast<int>(len))) {
                LOG_ERROR << "Failed to parse Heartbeat";
                return;
            }
            handleHeartbeat(conn, msg);
            break;
        }
        default:
            LOG_WARN << "Unknown message type: 0x" << std::hex << msg_type << std::dec;
            break;
    }
}

// ==================== 业务处理实现 ====================

void GatewayServer::handleTelemetry(const TcpConnectionPtr& conn,
                                    const proto::AgvTelemetry& msg) {
    const std::string& agv_id = msg.agv_id();
    
    // 1. 查找或创建会话
    AgvSessionPtr session = findSession(agv_id);
    if (!session) {
        registerSession(agv_id, conn);
        session = findSession(agv_id);
    }
    
    if (!session) {
        LOG_ERROR << "Failed to create session for AGV [" << agv_id << "]";
        return;
    }
    
    // 2. 更新会话状态
    session->updateActiveTime();
    session->updateBatteryLevel(msg.battery());
    session->updatePose(msg.x(), msg.y(), msg.theta(), msg.confidence());
    
    // 3. 触发基础业务引擎
    checkLowBatteryAndCharge(session, conn);
}

void GatewayServer::handleHeartbeat(const TcpConnectionPtr& conn,
                                    const proto::Heartbeat& msg) {
    const std::string& agv_id = msg.agv_id();
    
    // 查找或创建会话
    AgvSessionPtr session = findSession(agv_id);
    if (!session) {
        registerSession(agv_id, conn);
        session = findSession(agv_id);
    }
    
    if (!session) {
        LOG_ERROR << "Failed to create session for AGV [" << agv_id << "]";
        return;
    }
    
    // 刷新活跃时间
    session->updateActiveTime();
    LOG_DEBUG << "Heartbeat from [" << agv_id << "]";
}

// ==================== 会话管理实现 ====================

void GatewayServer::registerSession(const std::string& agv_id,
                                    const TcpConnectionPtr& conn) {
    MutexLockGuard lock(sessions_mutex_);
    
    if (sessions_.find(agv_id) != sessions_.end()) {
        LOG_WARN << "Session [" << agv_id << "] already exists, replacing";
    }
    
    sessions_[agv_id] = std::make_shared<AgvSession>(agv_id);
    connections_[agv_id] = conn;
    
    LOG_INFO << "Session registered: [" << agv_id << "] from "
             << conn->peerAddress().toIpPort();
}

void GatewayServer::removeSession(const std::string& agv_id) {
    MutexLockGuard lock(sessions_mutex_);
    sessions_.erase(agv_id);
    connections_.erase(agv_id);
    LOG_INFO << "Session removed: [" << agv_id << "]";
}

AgvSessionPtr GatewayServer::findSession(const std::string& agv_id) {
    MutexLockGuard lock(sessions_mutex_);
    auto it = sessions_.find(agv_id);
    return (it != sessions_.end()) ? it->second : nullptr;
}

// ==================== 上行看门狗实现 ====================

void GatewayServer::onWatchdogTimer() {
    Timestamp now = Timestamp::now();
    
    MutexLockGuard lock(sessions_mutex_);
    for (auto& pair : sessions_) {
        const std::string& agv_id = pair.first;
        AgvSessionPtr session = pair.second;
        
        // 计算距离最后活跃时间的间隔（毫秒）
        double elapsed_sec = timeDifference(now, session->getLastActiveTime());
        int64_t elapsed_ms = static_cast<int64_t>(elapsed_sec * 1000);
        
        // 超时检测
        if (elapsed_ms > kSessionTimeoutMs && session->getState() == AgvSession::ONLINE) {
            session->setState(AgvSession::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
                      << elapsed_ms << "ms > " << kSessionTimeoutMs << "ms)";
        }
    }
}

// ==================== 基础业务引擎实现 ====================

void GatewayServer::checkLowBatteryAndCharge(const AgvSessionPtr& session,
                                             const TcpConnectionPtr& conn) {
    double battery = session->getBatteryLevel();
    AgvSession::State state = session->getState();
    
    // 触发条件：电量 < 20% 且未在充电
    if (battery < kLowBatteryThreshold && state != AgvSession::CHARGING) {
        LOG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
                 << "] LOW BATTERY (" << battery << "%), sending charge command";
        
        sendChargeCommand(session->getAgvId(), conn);
        session->setState(AgvSession::CHARGING);  // 标记为充电中，避免重复下发
    }
}

// ==================== 消息发送实现 ====================

void GatewayServer::sendProtobufMessage(const TcpConnectionPtr& conn,
                                        uint16_t msg_type,
                                        const google::protobuf::Message& message) {
    // 1. 序列化 Protobuf
    std::string payload;
    if (!message.SerializeToString(&payload)) {
        LOG_ERROR << "Failed to serialize protobuf message";
        return;
    }
    
    // 2. 使用 LengthHeaderCodec 编码
    Buffer buf;
    if (!LengthHeaderCodec::encode(&buf, msg_type, payload)) {
        LOG_ERROR << "Failed to encode message";
        return;
    }
    
    // 3. 发送
    conn->send(&buf);
}

void GatewayServer::sendChargeCommand(const std::string& agv_id,
                                      const TcpConnectionPtr& conn) {
    proto::AgvCommand cmd;
    cmd.set_target_agv_id(agv_id);
    cmd.set_timestamp(Timestamp::now().microSecondsSinceEpoch());
    cmd.set_cmd_type(proto::CMD_EMERGENCY_STOP);  // 简化：暂用 EMERGENCY_STOP
    
    // 注意：按原文档，应下发 NavigationTask（目标 "CHARGER"）
    // 迭代三完善：发送完整的导航任务
    
    sendProtobufMessage(conn, proto::MSG_AGV_COMMAND, cmd);
    
    LOG_INFO << "[SEND] Charge command to [" << agv_id << "]";
}

}  // namespace gateway
}  // namespace agv