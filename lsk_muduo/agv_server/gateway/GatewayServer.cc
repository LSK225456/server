#include "GatewayServer.h"
#include "../../muduo/base/Logger.h"
#include "../codec/LengthHeaderCodec.h"
#include <arpa/inet.h>

namespace agv {
namespace gateway {

using namespace lsk_muduo;
using namespace agv::codec;

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

// ==================== 协议解析实现 ====================

// bool GatewayServer::parseHeader(Buffer* buf, uint16_t* msg_type, uint32_t* payload_len) {
//     if (buf->readableBytes() < 8) {
//         return false;  // 数据不足
//     }
//     
//     // 包头格式：| Length(4B) | MsgType(2B) | Flags(2B) |
//     // 注意：不要提前消费数据，先验证完整性
//     
//     // 1. 读取总长度（网络字节序已由 peekInt32 自动转换）
//     uint32_t total_len = static_cast<uint32_t>(buf->peekInt32());
//     
//     // 2. 校验合法性
//     if (total_len < 8 || total_len > 10 * 1024 * 1024) {  // 限制最大 10MB
//         LOG_ERROR << "Invalid message: total_len=" << total_len;
//         buf->retrieveAll();  // 丢弃所有数据，避免死循环
//         return false;
//     }
//     
//     // 3. 计算 Protobuf 负载长度
//     *payload_len = total_len - 8;
//     
//     // 4. 消费 Length 字段（4 字节）
//     buf->retrieve(4);
//     
//     // 5. 读取 MsgType（2 字节，网络字节序）
//     *msg_type = static_cast<uint16_t>(buf->readInt16());
//     
//     // 6. 跳过 Flags（2 字节）
//     buf->retrieve(2);
//     
//     return true;
// }

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
    session->updateActiveTime();
void GatewayServer::handleTelemetry(const TcpConnectionPtr& conn,
                                    const proto::AgvTelemetry& msg) {())
    const std::string& agv_id = msg.agv_id();
    // 3. 触发基础业务引擎
    // 1. 查找或创建会话ryAndCharge(session, conn);
    AgvSessionPtr session = findSession(agv_id);
    if (!session) {环境可关闭）
        registerSession(agv_id, conn); << agv_id << "] battery=" << msg.battery() << "%";
        session = findSession(agv_id);
    }
     GatewayServer::handleHeartbeat(const TcpConnectionPtr& conn,
    // 2. 更新会话状态                    const proto::Heartbeat& msg) {
    session->updateActiveTime();msg.agv_id();
    session->updateBatteryLevel(msg.battery());
    session->updatePose(msg.x(), msg.y(), msg.theta(), msg.confidence());
    AgvSessionPtr session = findSession(agv_id);
    // 3. 触发基础业务引擎{
    checkLowBatteryAndCharge(session, conn);
        session = findSession(agv_id);
    // 调试日志（高频消息，生产环境可关闭）
    // LOG_DEBUG << "Telemetry from [" << agv_id << "] battery=" << msg.battery() << "%";
}   // 刷新活跃时间
    session->updateActiveTime();
void GatewayServer::handleHeartbeat(const TcpConnectionPtr& conn,
                                    const proto::Heartbeat& msg) {
    const std::string& agv_id = msg.agv_id();
    =================== 会话管理实现 ====================
    // 查找或创建会话
    AgvSessionPtr session = findSession(agv_id);tring& agv_id,
    if (!session) {                 const TcpConnectionPtr& conn) {
        registerSession(agv_id, conn);_);
        session = findSession(agv_id);
    }f (sessions_.find(agv_id) != sessions_.end()) {
        LOG_WARN << "Session [" << agv_id << "] already exists, replacing";
    // 刷新活跃时间
    session->updateActiveTime();
    LOG_DEBUG << "Heartbeat from [" << agv_id << "]";agv_id);
}   connections_[agv_id] = conn;
    
// ==================== 会话管理实现 ====================< "] from "
             << conn->peerAddress().toIpPort();
void GatewayServer::registerSession(const std::string& agv_id,
                                    const TcpConnectionPtr& conn) {
    MutexLockGuard lock(sessions_mutex_);td::string& agv_id) {
    MutexLockGuard lock(sessions_mutex_);
    if (sessions_.find(agv_id) != sessions_.end()) {
        LOG_WARN << "Session [" << agv_id << "] already exists, replacing";
    }
    
    sessions_[agv_id] = std::make_shared<AgvSession>(agv_id);gv_id) {
    connections_[agv_id] = conn;_mutex_);
    auto it = sessions_.find(agv_id);
    LOG_INFO << "Session registered: [" << agv_id << "] from "
             << conn->peerAddress().toIpPort();
}
// ==================== 上行看门狗实现 ====================
void GatewayServer::removeSession(const std::string& agv_id) {
    MutexLockGuard lock(sessions_mutex_);
    sessions_.erase(agv_id);p::now();
    connections_.erase(agv_id);
}   MutexLockGuard lock(sessions_mutex_);
    for (auto& pair : sessions_) {
AgvSessionPtr GatewayServer::findSession(const std::string& agv_id) {
    MutexLockGuard lock(sessions_mutex_);nd;
    auto it = sessions_.find(agv_id);
    return (it != sessions_.end()) ? it->second : nullptr;
}       double elapsed_sec = timeDifference(now, session->getLastActiveTime());
        int64_t elapsed_ms = static_cast<int64_t>(elapsed_sec * 1000);
// ==================== 上行看门狗实现 ====================
        // 超时检测
void GatewayServer::onWatchdogTimer() {tMs && session->getState() == AgvSession::ONLINE) {
    Timestamp now = Timestamp::now();ion::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
    MutexLockGuard lock(sessions_mutex_);s > " << kSessionTimeoutMs << "ms)";
    for (auto& pair : sessions_) {
        const std::string& agv_id = pair.first;
        AgvSessionPtr session = pair.second;
        
        // 计算距离最后活跃时间的间隔（毫秒）引擎实现 ====================
        double elapsed_sec = timeDifference(now, session->getLastActiveTime());
        int64_t elapsed_ms = static_cast<int64_t>(elapsed_sec * 1000);ion,
                                             const TcpConnectionPtr& conn) {
        // 超时检测ery = session->getBatteryLevel();
        if (elapsed_ms > kSessionTimeoutMs && session->getState() == AgvSession::ONLINE) {
            session->setState(AgvSession::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
                      << elapsed_ms << "ms > " << kSessionTimeoutMs << "ms)";
        }OG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
    }            << "] LOW BATTERY (" << battery << "%), sending charge command";
}       
        sendChargeCommand(session->getAgvId(), conn);
// ==================== 基础业务引擎实现 ====================标记为充电中，避免重复下发
    }
void GatewayServer::checkLowBatteryAndCharge(const AgvSessionPtr& session,
                                             const TcpConnectionPtr& conn) {
    double battery = session->getBatteryLevel();===
    AgvSession::State state = session->getState();
     GatewayServer::sendProtobufMessage(const TcpConnectionPtr& conn,
    // 触发条件：电量 < 20% 且未在充电              uint16_t msg_type,
    if (battery < kLowBatteryThreshold && state != AgvSession::CHARGING) {essage) {
        LOG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
                 << "] LOW BATTERY (" << battery << "%), sending charge command";
        !message.SerializeToString(&payload)) {
        sendChargeCommand(session->getAgvId(), conn);ssage";
        session->setState(AgvSession::CHARGING);  // 标记为充电中，避免重复下发
    }
}   
    // 2. 使用 LengthHeaderCodec 编码
// ==================== 消息发送实现 ====================
    if (!LengthHeaderCodec::encode(&buf, msg_type, payload)) {
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

// ==================== 上行看门狗实现 ====================
        // 超时检测
void GatewayServer::onWatchdogTimer() {tMs && session->getState() == AgvSession::ONLINE) {
    Timestamp now = Timestamp::now();ion::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
    MutexLockGuard lock(sessions_mutex_);s > " << kSessionTimeoutMs << "ms)";
    for (auto& pair : sessions_) {
        const std::string& agv_id = pair.first;
        AgvSessionPtr session = pair.second;
        
        // 计算距离最后活跃时间的间隔（毫秒）引擎实现 ====================
        double elapsed_sec = timeDifference(now, session->getLastActiveTime());
        int64_t elapsed_ms = static_cast<int64_t>(elapsed_sec * 1000);ion,
                                             const TcpConnectionPtr& conn) {
        // 超时检测ery = session->getBatteryLevel();
        if (elapsed_ms > kSessionTimeoutMs && session->getState() == AgvSession::ONLINE) {
            session->setState(AgvSession::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
                      << elapsed_ms << "ms > " << kSessionTimeoutMs << "ms)";
        }OG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
    }            << "] LOW BATTERY (" << battery << "%), sending charge command";
}       
        sendChargeCommand(session->getAgvId(), conn);
// ==================== 基础业务引擎实现 ====================标记为充电中，避免重复下发
    }
void GatewayServer::checkLowBatteryAndCharge(const AgvSessionPtr& session,
                                             const TcpConnectionPtr& conn) {
    double battery = session->getBatteryLevel();===
    AgvSession::State state = session->getState();
     GatewayServer::sendProtobufMessage(const TcpConnectionPtr& conn,
    // 触发条件：电量 < 20% 且未在充电              uint16_t msg_type,
    if (battery < kLowBatteryThreshold && state != AgvSession::CHARGING) {essage) {
        LOG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
                 << "] LOW BATTERY (" << battery << "%), sending charge command";
        !message.SerializeToString(&payload)) {
        sendChargeCommand(session->getAgvId(), conn);ssage";
        session->setState(AgvSession::CHARGING);  // 标记为充电中，避免重复下发
    }
}   
    // 2. 使用 LengthHeaderCodec 编码
// ==================== 消息发送实现 ====================
    if (!LengthHeaderCodec::encode(&buf, msg_type, payload)) {
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

// ==================== 上行看门狗实现 ====================
        // 超时检测
void GatewayServer::onWatchdogTimer() {tMs && session->getState() == AgvSession::ONLINE) {
    Timestamp now = Timestamp::now();ion::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
    MutexLockGuard lock(sessions_mutex_);s > " << kSessionTimeoutMs << "ms)";
    for (auto& pair : sessions_) {
        const std::string& agv_id = pair.first;
        AgvSessionPtr session = pair.second;
        
        // 计算距离最后活跃时间的间隔（毫秒）引擎实现 ====================
        double elapsed_sec = timeDifference(now, session->getLastActiveTime());
        int64_t elapsed_ms = static_cast<int64_t>(elapsed_sec * 1000);ion,
                                             const TcpConnectionPtr& conn) {
        // 超时检测ery = session->getBatteryLevel();
        if (elapsed_ms > kSessionTimeoutMs && session->getState() == AgvSession::ONLINE) {
            session->setState(AgvSession::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
                      << elapsed_ms << "ms > " << kSessionTimeoutMs << "ms)";
        }OG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
    }            << "] LOW BATTERY (" << battery << "%), sending charge command";
}       
        sendChargeCommand(session->getAgvId(), conn);
// ==================== 基础业务引擎实现 ====================标记为充电中，避免重复下发
    }
void GatewayServer::checkLowBatteryAndCharge(const AgvSessionPtr& session,
                                             const TcpConnectionPtr& conn) {
    double battery = session->getBatteryLevel();===
    AgvSession::State state = session->getState();
     GatewayServer::sendProtobufMessage(const TcpConnectionPtr& conn,
    // 触发条件：电量 < 20% 且未在充电              uint16_t msg_type,
    if (battery < kLowBatteryThreshold && state != AgvSession::CHARGING) {essage) {
        LOG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
                 << "] LOW BATTERY (" << battery << "%), sending charge command";
        !message.SerializeToString(&payload)) {
        sendChargeCommand(session->getAgvId(), conn);ssage";
        session->setState(AgvSession::CHARGING);  // 标记为充电中，避免重复下发
    }
}   
    // 2. 使用 LengthHeaderCodec 编码
// ==================== 消息发送实现 ====================
    if (!LengthHeaderCodec::encode(&buf, msg_type, payload)) {
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

// ==================== 上行看门狗实现 ====================
        // 超时检测
void GatewayServer::onWatchdogTimer() {tMs && session->getState() == AgvSession::ONLINE) {
    Timestamp now = Timestamp::now();ion::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
    MutexLockGuard lock(sessions_mutex_);s > " << kSessionTimeoutMs << "ms)";
    for (auto& pair : sessions_) {
        const std::string& agv_id = pair.first;
        AgvSessionPtr session = pair.second;
        
        // 计算距离最后活跃时间的间隔（毫秒）引擎实现 ====================
        double elapsed_sec = timeDifference(now, session->getLastActiveTime());
        int64_t elapsed_ms = static_cast<int64_t>(elapsed_sec * 1000);ion,
                                             const TcpConnectionPtr& conn) {
        // 超时检测ery = session->getBatteryLevel();
        if (elapsed_ms > kSessionTimeoutMs && session->getState() == AgvSession::ONLINE) {
            session->setState(AgvSession::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
                      << elapsed_ms << "ms > " << kSessionTimeoutMs << "ms)";
        }OG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
    }            << "] LOW BATTERY (" << battery << "%), sending charge command";
}       
        sendChargeCommand(session->getAgvId(), conn);
// ==================== 基础业务引擎实现 ====================标记为充电中，避免重复下发
    }
void GatewayServer::checkLowBatteryAndCharge(const AgvSessionPtr& session,
                                             const TcpConnectionPtr& conn) {
    double battery = session->getBatteryLevel();===
    AgvSession::State state = session->getState();
     GatewayServer::sendProtobufMessage(const TcpConnectionPtr& conn,
    // 触发条件：电量 < 20% 且未在充电              uint16_t msg_type,
    if (battery < kLowBatteryThreshold && state != AgvSession::CHARGING) {essage) {
        LOG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
                 << "] LOW BATTERY (" << battery << "%), sending charge command";
        !message.SerializeToString(&payload)) {
        sendChargeCommand(session->getAgvId(), conn);ssage";
        session->setState(AgvSession::CHARGING);  // 标记为充电中，避免重复下发
    }
}   
    // 2. 使用 LengthHeaderCodec 编码
// ==================== 消息发送实现 ====================
    if (!LengthHeaderCodec::encode(&buf, msg_type, payload)) {
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

// ==================== 上行看门狗实现 ====================
        // 超时检测
void GatewayServer::onWatchdogTimer() {tMs && session->getState() == AgvSession::ONLINE) {
    Timestamp now = Timestamp::now();ion::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
    MutexLockGuard lock(sessions_mutex_);s > " << kSessionTimeoutMs << "ms)";
    for (auto& pair : sessions_) {
        const std::string& agv_id = pair.first;
        AgvSessionPtr session = pair.second;
        
        // 计算距离最后活跃时间的间隔（毫秒）引擎实现 ====================
        double elapsed_sec = timeDifference(now, session->getLastActiveTime());
        int64_t elapsed_ms = static_cast<int64_t>(elapsed_sec * 1000);ion,
                                             const TcpConnectionPtr& conn) {
        // 超时检测ery = session->getBatteryLevel();
        if (elapsed_ms > kSessionTimeoutMs && session->getState() == AgvSession::ONLINE) {
            session->setState(AgvSession::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
                      << elapsed_ms << "ms > " << kSessionTimeoutMs << "ms)";
        }OG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
    }            << "] LOW BATTERY (" << battery << "%), sending charge command";
}       
        sendChargeCommand(session->getAgvId(), conn);
// ==================== 基础业务引擎实现 ====================标记为充电中，避免重复下发
    }
void GatewayServer::checkLowBatteryAndCharge(const AgvSessionPtr& session,
                                             const TcpConnectionPtr& conn) {
    double battery = session->getBatteryLevel();===
    AgvSession::State state = session->getState();
     GatewayServer::sendProtobufMessage(const TcpConnectionPtr& conn,
    // 触发条件：电量 < 20% 且未在充电              uint16_t msg_type,
    if (battery < kLowBatteryThreshold && state != AgvSession::CHARGING) {essage) {
        LOG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
                 << "] LOW BATTERY (" << battery << "%), sending charge command";
        !message.SerializeToString(&payload)) {
        sendChargeCommand(session->getAgvId(), conn);ssage";
        session->setState(AgvSession::CHARGING);  // 标记为充电中，避免重复下发
    }
}   
    // 2. 使用 LengthHeaderCodec 编码
// ==================== 消息发送实现 ====================
    if (!LengthHeaderCodec::encode(&buf, msg_type, payload)) {
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

// ==================== 上行看门狗实现 ====================
        // 超时检测
void GatewayServer::onWatchdogTimer() {tMs && session->getState() == AgvSession::ONLINE) {
    Timestamp now = Timestamp::now();ion::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
    MutexLockGuard lock(sessions_mutex_);s > " << kSessionTimeoutMs << "ms)";
    for (auto& pair : sessions_) {
        const std::string& agv_id = pair.first;
        AgvSessionPtr session = pair.second;
        
        // 计算距离最后活跃时间的间隔（毫秒）引擎实现 ====================
        double elapsed_sec = timeDifference(now, session->getLastActiveTime());
        int64_t elapsed_ms = static_cast<int64_t>(elapsed_sec * 1000);ion,
                                             const TcpConnectionPtr& conn) {
        // 超时检测ery = session->getBatteryLevel();
        if (elapsed_ms > kSessionTimeoutMs && session->getState() == AgvSession::ONLINE) {
            session->setState(AgvSession::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
                      << elapsed_ms << "ms > " << kSessionTimeoutMs << "ms)";
        }OG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
    }            << "] LOW BATTERY (" << battery << "%), sending charge command";
}       
        sendChargeCommand(session->getAgvId(), conn);
// ==================== 基础业务引擎实现 ====================标记为充电中，避免重复下发
    }
void GatewayServer::checkLowBatteryAndCharge(const AgvSessionPtr& session,
                                             const TcpConnectionPtr& conn) {
    double battery = session->getBatteryLevel();===
    AgvSession::State state = session->getState();
     GatewayServer::sendProtobufMessage(const TcpConnectionPtr& conn,
    // 触发条件：电量 < 20% 且未在充电              uint16_t msg_type,
    if (battery < kLowBatteryThreshold && state != AgvSession::CHARGING) {essage) {
        LOG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
                 << "] LOW BATTERY (" << battery << "%), sending charge command";
        !message.SerializeToString(&payload)) {
        sendChargeCommand(session->getAgvId(), conn);ssage";
        session->setState(AgvSession::CHARGING);  // 标记为充电中，避免重复下发
    }
}   
    // 2. 使用 LengthHeaderCodec 编码
// ==================== 消息发送实现 ====================
    if (!LengthHeaderCodec::encode(&buf, msg_type, payload)) {
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

// ==================== 上行看门狗实现 ====================
        // 超时检测
void GatewayServer::onWatchdogTimer() {tMs && session->getState() == AgvSession::ONLINE) {
    Timestamp now = Timestamp::now();ion::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
    MutexLockGuard lock(sessions_mutex_);s > " << kSessionTimeoutMs << "ms)";
    for (auto& pair : sessions_) {
        const std::string& agv_id = pair.first;
        AgvSessionPtr session = pair.second;
        
        // 计算距离最后活跃时间的间隔（毫秒）引擎实现 ====================
        double elapsed_sec = timeDifference(now, session->getLastActiveTime());
        int64_t elapsed_ms = static_cast<int64_t>(elapsed_sec * 1000);ion,
                                             const TcpConnectionPtr& conn) {
        // 超时检测ery = session->getBatteryLevel();
        if (elapsed_ms > kSessionTimeoutMs && session->getState() == AgvSession::ONLINE) {
            session->setState(AgvSession::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
                      << elapsed_ms << "ms > " << kSessionTimeoutMs << "ms)";
        }OG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
    }            << "] LOW BATTERY (" << battery << "%), sending charge command";
}       
        sendChargeCommand(session->getAgvId(), conn);
// ==================== 基础业务引擎实现 ====================标记为充电中，避免重复下发
    }
void GatewayServer::checkLowBatteryAndCharge(const AgvSessionPtr& session,
                                             const TcpConnectionPtr& conn) {
    double battery = session->getBatteryLevel();===
    AgvSession::State state = session->getState();
     GatewayServer::sendProtobufMessage(const TcpConnectionPtr& conn,
    // 触发条件：电量 < 20% 且未在充电              uint16_t msg_type,
    if (battery < kLowBatteryThreshold && state != AgvSession::CHARGING) {essage) {
        LOG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
                 << "] LOW BATTERY (" << battery << "%), sending charge command";
        !message.SerializeToString(&payload)) {
        sendChargeCommand(session->getAgvId(), conn);ssage";
        session->setState(AgvSession::CHARGING);  // 标记为充电中，避免重复下发
    }
}   
    // 2. 使用 LengthHeaderCodec 编码
// ==================== 消息发送实现 ====================
    if (!LengthHeaderCodec::encode(&buf, msg_type, payload)) {
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

// ==================== 上行看门狗实现 ====================
        // 超时检测
void GatewayServer::onWatchdogTimer() {tMs && session->getState() == AgvSession::ONLINE) {
    Timestamp now = Timestamp::now();ion::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
    MutexLockGuard lock(sessions_mutex_);s > " << kSessionTimeoutMs << "ms)";
    for (auto& pair : sessions_) {
        const std::string& agv_id = pair.first;
        AgvSessionPtr session = pair.second;
        
        // 计算距离最后活跃时间的间隔（毫秒）引擎实现 ====================
        double elapsed_sec = timeDifference(now, session->getLastActiveTime());
        int64_t elapsed_ms = static_cast<int64_t>(elapsed_sec * 1000);ion,
                                             const TcpConnectionPtr& conn) {
        // 超时检测ery = session->getBatteryLevel();
        if (elapsed_ms > kSessionTimeoutMs && session->getState() == AgvSession::ONLINE) {
            session->setState(AgvSession::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
                      << elapsed_ms << "ms > " << kSessionTimeoutMs << "ms)";
        }OG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
    }            << "] LOW BATTERY (" << battery << "%), sending charge command";
}       
        sendChargeCommand(session->getAgvId(), conn);
// ==================== 基础业务引擎实现 ====================标记为充电中，避免重复下发
    }
void GatewayServer::checkLowBatteryAndCharge(const AgvSessionPtr& session,
                                             const TcpConnectionPtr& conn) {
    double battery = session->getBatteryLevel();===
    AgvSession::State state = session->getState();
     GatewayServer::sendProtobufMessage(const TcpConnectionPtr& conn,
    // 触发条件：电量 < 20% 且未在充电              uint16_t msg_type,
    if (battery < kLowBatteryThreshold && state != AgvSession::CHARGING) {essage) {
        LOG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
                 << "] LOW BATTERY (" << battery << "%), sending charge command";
        !message.SerializeToString(&payload)) {
        sendChargeCommand(session->getAgvId(), conn);ssage";
        session->setState(AgvSession::CHARGING);  // 标记为充电中，避免重复下发
    }
}   
    // 2. 使用 LengthHeaderCodec 编码
// ==================== 消息发送实现 ====================
    if (!LengthHeaderCodec::encode(&buf, msg_type, payload)) {
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

// ==================== 上行看门狗实现 ====================
        // 超时检测
void GatewayServer::onWatchdogTimer() {tMs && session->getState() == AgvSession::ONLINE) {
    Timestamp now = Timestamp::now();ion::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
    MutexLockGuard lock(sessions_mutex_);s > " << kSessionTimeoutMs << "ms)";
    for (auto& pair : sessions_) {
        const std::string& agv_id = pair.first;
        AgvSessionPtr session = pair.second;
        
        // 计算距离最后活跃时间的间隔（毫秒）引擎实现 ====================
        double elapsed_sec = timeDifference(now, session->getLastActiveTime());
        int64_t elapsed_ms = static_cast<int64_t>(elapsed_sec * 1000);ion,
                                             const TcpConnectionPtr& conn) {
        // 超时检测ery = session->getBatteryLevel();
        if (elapsed_ms > kSessionTimeoutMs && session->getState() == AgvSession::ONLINE) {
            session->setState(AgvSession::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
                      << elapsed_ms << "ms > " << kSessionTimeoutMs << "ms)";
        }OG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
    }            << "] LOW BATTERY (" << battery << "%), sending charge command";
}       
        sendChargeCommand(session->getAgvId(), conn);
// ==================== 基础业务引擎实现 ====================标记为充电中，避免重复下发
    }
void GatewayServer::checkLowBatteryAndCharge(const AgvSessionPtr& session,
                                             const TcpConnectionPtr& conn) {
    double battery = session->getBatteryLevel();===
    AgvSession::State state = session->getState();
     GatewayServer::sendProtobufMessage(const TcpConnectionPtr& conn,
    // 触发条件：电量 < 20% 且未在充电              uint16_t msg_type,
    if (battery < kLowBatteryThreshold && state != AgvSession::CHARGING) {essage) {
        LOG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
                 << "] LOW BATTERY (" << battery << "%), sending charge command";
        !message.SerializeToString(&payload)) {
        sendChargeCommand(session->getAgvId(), conn);ssage";
        session->setState(AgvSession::CHARGING);  // 标记为充电中，避免重复下发
    }
}   
    // 2. 使用 LengthHeaderCodec 编码
// ==================== 消息发送实现 ====================
    if (!LengthHeaderCodec::encode(&buf, msg_type, payload)) {
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

// ==================== 上行看门狗实现 ====================
        // 超时检测
void GatewayServer::onWatchdogTimer() {tMs && session->getState() == AgvSession::ONLINE) {
    Timestamp now = Timestamp::now();ion::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
    MutexLockGuard lock(sessions_mutex_);s > " << kSessionTimeoutMs << "ms)";
    for (auto& pair : sessions_) {
        const std::string& agv_id = pair.first;
        AgvSessionPtr session = pair.second;
        
        // 计算距离最后活跃时间的间隔（毫秒）引擎实现 ====================
        double elapsed_sec = timeDifference(now, session->getLastActiveTime());
        int64_t elapsed_ms = static_cast<int64_t>(elapsed_sec * 1000);ion,
                                             const TcpConnectionPtr& conn) {
        // 超时检测ery = session->getBatteryLevel();
        if (elapsed_ms > kSessionTimeoutMs && session->getState() == AgvSession::ONLINE) {
            session->setState(AgvSession::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
                      << elapsed_ms << "ms > " << kSessionTimeoutMs << "ms)";
        }OG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
    }            << "] LOW BATTERY (" << battery << "%), sending charge command";
}       
        sendChargeCommand(session->getAgvId(), conn);
// ==================== 基础业务引擎实现 ====================标记为充电中，避免重复下发
    }
void GatewayServer::checkLowBatteryAndCharge(const AgvSessionPtr& session,
                                             const TcpConnectionPtr& conn) {
    double battery = session->getBatteryLevel();===
    AgvSession::State state = session->getState();
     GatewayServer::sendProtobufMessage(const TcpConnectionPtr& conn,
    // 触发条件：电量 < 20% 且未在充电              uint16_t msg_type,
    if (battery < kLowBatteryThreshold && state != AgvSession::CHARGING) {essage) {
        LOG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
                 << "] LOW BATTERY (" << battery << "%), sending charge command";
        !message.SerializeToString(&payload)) {
        sendChargeCommand(session->getAgvId(), conn);ssage";
        session->setState(AgvSession::CHARGING);  // 标记为充电中，避免重复下发
    }
}   
    // 2. 使用 LengthHeaderCodec 编码
// ==================== 消息发送实现 ====================
    if (!LengthHeaderCodec::encode(&buf, msg_type, payload)) {
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

// ==================== 上行看门狗实现 ====================
        // 超时检测
void GatewayServer::onWatchdogTimer() {tMs && session->getState() == AgvSession::ONLINE) {
    Timestamp now = Timestamp::now();ion::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
    MutexLockGuard lock(sessions_mutex_);s > " << kSessionTimeoutMs << "ms)";
    for (auto& pair : sessions_) {
        const std::string& agv_id = pair.first;
        AgvSessionPtr session = pair.second;
        
        // 计算距离最后活跃时间的间隔（毫秒）引擎实现 ====================
        double elapsed_sec = timeDifference(now, session->getLastActiveTime());
        int64_t elapsed_ms = static_cast<int64_t>(elapsed_sec * 1000);ion,
                                             const TcpConnectionPtr& conn) {
        // 超时检测ery = session->getBatteryLevel();
        if (elapsed_ms > kSessionTimeoutMs && session->getState() == AgvSession::ONLINE) {
            session->setState(AgvSession::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
                      << elapsed_ms << "ms > " << kSessionTimeoutMs << "ms)";
        }OG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
    }            << "] LOW BATTERY (" << battery << "%), sending charge command";
}       
        sendChargeCommand(session->getAgvId(), conn);
// ==================== 基础业务引擎实现 ====================标记为充电中，避免重复下发
    }
void GatewayServer::checkLowBatteryAndCharge(const AgvSessionPtr& session,
                                             const TcpConnectionPtr& conn) {
    double battery = session->getBatteryLevel();===
    AgvSession::State state = session->getState();
     GatewayServer::sendProtobufMessage(const TcpConnectionPtr& conn,
    // 触发条件：电量 < 20% 且未在充电              uint16_t msg_type,
    if (battery < kLowBatteryThreshold && state != AgvSession::CHARGING) {essage) {
        LOG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
                 << "] LOW BATTERY (" << battery << "%), sending charge command";
        !message.SerializeToString(&payload)) {
        sendChargeCommand(session->getAgvId(), conn);ssage";
        session->setState(AgvSession::CHARGING);  // 标记为充电中，避免重复下发
    }
}   
    // 2. 使用 LengthHeaderCodec 编码
// ==================== 消息发送实现 ====================
    if (!LengthHeaderCodec::encode(&buf, msg_type, payload)) {
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

// ==================== 上行看门狗实现 ====================
        // 超时检测
void GatewayServer::onWatchdogTimer() {tMs && session->getState() == AgvSession::ONLINE) {
    Timestamp now = Timestamp::now();ion::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
    MutexLockGuard lock(sessions_mutex_);s > " << kSessionTimeoutMs << "ms)";
    for (auto& pair : sessions_) {
        const std::string& agv_id = pair.first;
        AgvSessionPtr session = pair.second;
        
        // 计算距离最后活跃时间的间隔（毫秒）引擎实现 ====================
        double elapsed_sec = timeDifference(now, session->getLastActiveTime());
        int64_t elapsed_ms = static_cast<int64_t>(elapsed_sec * 1000);ion,
                                             const TcpConnectionPtr& conn) {
        // 超时检测ery = session->getBatteryLevel();
        if (elapsed_ms > kSessionTimeoutMs && session->getState() == AgvSession::ONLINE) {
            session->setState(AgvSession::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
                      << elapsed_ms << "ms > " << kSessionTimeoutMs << "ms)";
        }OG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
    }            << "] LOW BATTERY (" << battery << "%), sending charge command";
}       
        sendChargeCommand(session->getAgvId(), conn);
// ==================== 基础业务引擎实现 ====================标记为充电中，避免重复下发
    }
void GatewayServer::checkLowBatteryAndCharge(const AgvSessionPtr& session,
                                             const TcpConnectionPtr& conn) {
    double battery = session->getBatteryLevel();===
    AgvSession::State state = session->getState();
     GatewayServer::sendProtobufMessage(const TcpConnectionPtr& conn,
    // 触发条件：电量 < 20% 且未在充电              uint16_t msg_type,
    if (battery < kLowBatteryThreshold && state != AgvSession::CHARGING) {essage) {
        LOG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
                 << "] LOW BATTERY (" << battery << "%), sending charge command";
        !message.SerializeToString(&payload)) {
        sendChargeCommand(session->getAgvId(), conn);ssage";
        session->setState(AgvSession::CHARGING);  // 标记为充电中，避免重复下发
    }
}   
    // 2. 使用 LengthHeaderCodec 编码
// ==================== 消息发送实现 ====================
    if (!LengthHeaderCodec::encode(&buf, msg_type, payload)) {
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

// ==================== 上行看门狗实现 ====================
        // 超时检测
void GatewayServer::onWatchdogTimer() {tMs && session->getState() == AgvSession::ONLINE) {
    Timestamp now = Timestamp::now();ion::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
    MutexLockGuard lock(sessions_mutex_);s > " << kSessionTimeoutMs << "ms)";
    for (auto& pair : sessions_) {
        const std::string& agv_id = pair.first;
        AgvSessionPtr session = pair.second;
        
        // 计算距离最后活跃时间的间隔（毫秒）引擎实现 ====================
        double elapsed_sec = timeDifference(now, session->getLastActiveTime());
        int64_t elapsed_ms = static_cast<int64_t>(elapsed_sec * 1000);ion,
                                             const TcpConnectionPtr& conn) {
        // 超时检测ery = session->getBatteryLevel();
        if (elapsed_ms > kSessionTimeoutMs && session->getState() == AgvSession::ONLINE) {
            session->setState(AgvSession::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
                      << elapsed_ms << "ms > " << kSessionTimeoutMs << "ms)";
        }OG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
    }            << "] LOW BATTERY (" << battery << "%), sending charge command";
}       
        sendChargeCommand(session->getAgvId(), conn);
// ==================== 基础业务引擎实现 ====================标记为充电中，避免重复下发
    }
void GatewayServer::checkLowBatteryAndCharge(const AgvSessionPtr& session,
                                             const TcpConnectionPtr& conn) {
    double battery = session->getBatteryLevel();===
    AgvSession::State state = session->getState();
     GatewayServer::sendProtobufMessage(const TcpConnectionPtr& conn,
    // 触发条件：电量 < 20% 且未在充电              uint16_t msg_type,
    if (battery < kLowBatteryThreshold && state != AgvSession::CHARGING) {essage) {
        LOG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
                 << "] LOW BATTERY (" << battery << "%), sending charge command";
        !message.SerializeToString(&payload)) {
        sendChargeCommand(session->getAgvId(), conn);ssage";
        session->setState(AgvSession::CHARGING);  // 标记为充电中，避免重复下发
    }
}   
    // 2. 使用 LengthHeaderCodec 编码
// ==================== 消息发送实现 ====================
    if (!LengthHeaderCodec::encode(&buf, msg_type, payload)) {
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

// ==================== 上行看门狗实现 ====================
        // 超时检测
void GatewayServer::onWatchdogTimer() {tMs && session->getState() == AgvSession::ONLINE) {
    Timestamp now = Timestamp::now();ion::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
    MutexLockGuard lock(sessions_mutex_);s > " << kSessionTimeoutMs << "ms)";
    for (auto& pair : sessions_) {
        const std::string& agv_id = pair.first;
        AgvSessionPtr session = pair.second;
        
        // 计算距离最后活跃时间的间隔（毫秒）引擎实现 ====================
        double elapsed_sec = timeDifference(now, session->getLastActiveTime());
        int64_t elapsed_ms = static_cast<int64_t>(elapsed_sec * 1000);ion,
                                             const TcpConnectionPtr& conn) {
        // 超时检测ery = session->getBatteryLevel();
        if (elapsed_ms > kSessionTimeoutMs && session->getState() == AgvSession::ONLINE) {
            session->setState(AgvSession::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
                      << elapsed_ms << "ms > " << kSessionTimeoutMs << "ms)";
        }OG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
    }            << "] LOW BATTERY (" << battery << "%), sending charge command";
}       
        sendChargeCommand(session->getAgvId(), conn);
// ==================== 基础业务引擎实现 ====================标记为充电中，避免重复下发
    }
void GatewayServer::checkLowBatteryAndCharge(const AgvSessionPtr& session,
                                             const TcpConnectionPtr& conn) {
    double battery = session->getBatteryLevel();===
    AgvSession::State state = session->getState();
     GatewayServer::sendProtobufMessage(const TcpConnectionPtr& conn,
    // 触发条件：电量 < 20% 且未在充电              uint16_t msg_type,
    if (battery < kLowBatteryThreshold && state != AgvSession::CHARGING) {essage) {
        LOG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
                 << "] LOW BATTERY (" << battery << "%), sending charge command";
        !message.SerializeToString(&payload)) {
        sendChargeCommand(session->getAgvId(), conn);ssage";
        session->setState(AgvSession::CHARGING);  // 标记为充电中，避免重复下发
    }
}   
    // 2. 使用 LengthHeaderCodec 编码
// ==================== 消息发送实现 ====================
    if (!LengthHeaderCodec::encode(&buf, msg_type, payload)) {
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

// ==================== 上行看门狗实现 ====================
        // 超时检测
void GatewayServer::onWatchdogTimer() {tMs && session->getState() == AgvSession::ONLINE) {
    Timestamp now = Timestamp::now();ion::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
    MutexLockGuard lock(sessions_mutex_);s > " << kSessionTimeoutMs << "ms)";
    for (auto& pair : sessions_) {
        const std::string& agv_id = pair.first;
        AgvSessionPtr session = pair.second;
        
        // 计算距离最后活跃时间的间隔（毫秒）引擎实现 ====================
        double elapsed_sec = timeDifference(now, session->getLastActiveTime());
        int64_t elapsed_ms = static_cast<int64_t>(elapsed_sec * 1000);ion,
                                             const TcpConnectionPtr& conn) {
        // 超时检测ery = session->getBatteryLevel();
        if (elapsed_ms > kSessionTimeoutMs && session->getState() == AgvSession::ONLINE) {
            session->setState(AgvSession::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
                      << elapsed_ms << "ms > " << kSessionTimeoutMs << "ms)";
        }OG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
    }            << "] LOW BATTERY (" << battery << "%), sending charge command";
}       
        sendChargeCommand(session->getAgvId(), conn);
// ==================== 基础业务引擎实现 ====================标记为充电中，避免重复下发
    }
void GatewayServer::checkLowBatteryAndCharge(const AgvSessionPtr& session,
                                             const TcpConnectionPtr& conn) {
    double battery = session->getBatteryLevel();===
    AgvSession::State state = session->getState();
     GatewayServer::sendProtobufMessage(const TcpConnectionPtr& conn,
    // 触发条件：电量 < 20% 且未在充电              uint16_t msg_type,
    if (battery < kLowBatteryThreshold && state != AgvSession::CHARGING) {essage) {
        LOG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
                 << "] LOW BATTERY (" << battery << "%), sending charge command";
        !message.SerializeToString(&payload)) {
        sendChargeCommand(session->getAgvId(), conn);ssage";
        session->setState(AgvSession::CHARGING);  // 标记为充电中，避免重复下发
    }
}   
    // 2. 使用 LengthHeaderCodec 编码
// ==================== 消息发送实现 ====================
    if (!LengthHeaderCodec::encode(&buf, msg_type, payload)) {
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

// ==================== 上行看门狗实现 ====================
        // 超时检测
void GatewayServer::onWatchdogTimer() {tMs && session->getState() == AgvSession::ONLINE) {
    Timestamp now = Timestamp::now();ion::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
    MutexLockGuard lock(sessions_mutex_);s > " << kSessionTimeoutMs << "ms)";
    for (auto& pair : sessions_) {
        const std::string& agv_id = pair.first;
        AgvSessionPtr session = pair.second;
        
        // 计算距离最后活跃时间的间隔（毫秒）引擎实现 ====================
        double elapsed_sec = timeDifference(now, session->getLastActiveTime());
        int64_t elapsed_ms = static_cast<int64_t>(elapsed_sec * 1000);ion,
                                             const TcpConnectionPtr& conn) {
        // 超时检测ery = session->getBatteryLevel();
        if (elapsed_ms > kSessionTimeoutMs && session->getState() == AgvSession::ONLINE) {
            session->setState(AgvSession::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
                      << elapsed_ms << "ms > " << kSessionTimeoutMs << "ms)";
        }OG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
    }            << "] LOW BATTERY (" << battery << "%), sending charge command";
}       
        sendChargeCommand(session->getAgvId(), conn);
// ==================== 基础业务引擎实现 ====================标记为充电中，避免重复下发
    }
void GatewayServer::checkLowBatteryAndCharge(const AgvSessionPtr& session,
                                             const TcpConnectionPtr& conn) {
    double battery = session->getBatteryLevel();===
    AgvSession::State state = session->getState();
     GatewayServer::sendProtobufMessage(const TcpConnectionPtr& conn,
    // 触发条件：电量 < 20% 且未在充电              uint16_t msg_type,
    if (battery < kLowBatteryThreshold && state != AgvSession::CHARGING) {essage) {
        LOG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
                 << "] LOW BATTERY (" << battery << "%), sending charge command";
        !message.SerializeToString(&payload)) {
        sendChargeCommand(session->getAgvId(), conn);ssage";
        session->setState(AgvSession::CHARGING);  // 标记为充电中，避免重复下发
    }
}   
    // 2. 使用 LengthHeaderCodec 编码
// ==================== 消息发送实现 ====================
    if (!LengthHeaderCodec::encode(&buf, msg_type, payload)) {
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

// ==================== 上行看门狗实现 ====================
        // 超时检测
void GatewayServer::onWatchdogTimer() {tMs && session->getState() == AgvSession::ONLINE) {
    Timestamp now = Timestamp::now();ion::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
    MutexLockGuard lock(sessions_mutex_);s > " << kSessionTimeoutMs << "ms)";
    for (auto& pair : sessions_) {
        const std::string& agv_id = pair.first;
        AgvSessionPtr session = pair.second;
        
        // 计算距离最后活跃时间的间隔（毫秒）引擎实现 ====================
        double elapsed_sec = timeDifference(now, session->getLastActiveTime());
        int64_t elapsed_ms = static_cast<int64_t>(elapsed_sec * 1000);ion,
                                             const TcpConnectionPtr& conn) {
        // 超时检测ery = session->getBatteryLevel();
        if (elapsed_ms > kSessionTimeoutMs && session->getState() == AgvSession::ONLINE) {
            session->setState(AgvSession::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
                      << elapsed_ms << "ms > " << kSessionTimeoutMs << "ms)";
        }OG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
    }            << "] LOW BATTERY (" << battery << "%), sending charge command";
}       
        sendChargeCommand(session->getAgvId(), conn);
// ==================== 基础业务引擎实现 ====================标记为充电中，避免重复下发
    }
void GatewayServer::checkLowBatteryAndCharge(const AgvSessionPtr& session,
                                             const TcpConnectionPtr& conn) {
    double battery = session->getBatteryLevel();===
    AgvSession::State state = session->getState();
     GatewayServer::sendProtobufMessage(const TcpConnectionPtr& conn,
    // 触发条件：电量 < 20% 且未在充电              uint16_t msg_type,
    if (battery < kLowBatteryThreshold && state != AgvSession::CHARGING) {essage) {
        LOG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
                 << "] LOW BATTERY (" << battery << "%), sending charge command";
        !message.SerializeToString(&payload)) {
        sendChargeCommand(session->getAgvId(), conn);ssage";
        session->setState(AgvSession::CHARGING);  // 标记为充电中，避免重复下发
    }
}   
    // 2. 使用 LengthHeaderCodec 编码
// ==================== 消息发送实现 ====================
    if (!LengthHeaderCodec::encode(&buf, msg_type, payload)) {
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

// ==================== 上行看门狗实现 ====================
        // 超时检测
void GatewayServer::onWatchdogTimer() {tMs && session->getState() == AgvSession::ONLINE) {
    Timestamp now = Timestamp::now();ion::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
    MutexLockGuard lock(sessions_mutex_);s > " << kSessionTimeoutMs << "ms)";
    for (auto& pair : sessions_) {
        const std::string& agv_id = pair.first;
        AgvSessionPtr session = pair.second;
        
        // 计算距离最后活跃时间的间隔（毫秒）引擎实现 ====================
        double elapsed_sec = timeDifference(now, session->getLastActiveTime());
        int64_t elapsed_ms = static_cast<int64_t>(elapsed_sec * 1000);ion,
                                             const TcpConnectionPtr& conn) {
        // 超时检测ery = session->getBatteryLevel();
        if (elapsed_ms > kSessionTimeoutMs && session->getState() == AgvSession::ONLINE) {
            session->setState(AgvSession::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
                      << elapsed_ms << "ms > " << kSessionTimeoutMs << "ms)";
        }OG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
    }            << "] LOW BATTERY (" << battery << "%), sending charge command";
}       
        sendChargeCommand(session->getAgvId(), conn);
// ==================== 基础业务引擎实现 ====================标记为充电中，避免重复下发
    }
void GatewayServer::checkLowBatteryAndCharge(const AgvSessionPtr& session,
                                             const TcpConnectionPtr& conn) {
    double battery = session->getBatteryLevel();===
    AgvSession::State state = session->getState();
     GatewayServer::sendProtobufMessage(const TcpConnectionPtr& conn,
    // 触发条件：电量 < 20% 且未在充电              uint16_t msg_type,
    if (battery < kLowBatteryThreshold && state != AgvSession::CHARGING) {essage) {
        LOG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
                 << "] LOW BATTERY (" << battery << "%), sending charge command";
        !message.SerializeToString(&payload)) {
        sendChargeCommand(session->getAgvId(), conn);ssage";
        session->setState(AgvSession::CHARGING);  // 标记为充电中，避免重复下发
    }
}   
    // 2. 使用 LengthHeaderCodec 编码
// ==================== 消息发送实现 ====================
    if (!LengthHeaderCodec::encode(&buf, msg_type, payload)) {
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

// ==================== 上行看门狗实现 ====================
        // 超时检测
void GatewayServer::onWatchdogTimer() {tMs && session->getState() == AgvSession::ONLINE) {
    Timestamp now = Timestamp::now();ion::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
    MutexLockGuard lock(sessions_mutex_);s > " << kSessionTimeoutMs << "ms)";
    for (auto& pair : sessions_) {
        const std::string& agv_id = pair.first;
        AgvSessionPtr session = pair.second;
        
        // 计算距离最后活跃时间的间隔（毫秒）引擎实现 ====================
        double elapsed_sec = timeDifference(now, session->getLastActiveTime());
        int64_t elapsed_ms = static_cast<int64_t>(elapsed_sec * 1000);ion,
                                             const TcpConnectionPtr& conn) {
        // 超时检测ery = session->getBatteryLevel();
        if (elapsed_ms > kSessionTimeoutMs && session->getState() == AgvSession::ONLINE) {
            session->setState(AgvSession::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
                      << elapsed_ms << "ms > " << kSessionTimeoutMs << "ms)";
        }OG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
    }            << "] LOW BATTERY (" << battery << "%), sending charge command";
}       
        sendChargeCommand(session->getAgvId(), conn);
// ==================== 基础业务引擎实现 ====================标记为充电中，避免重复下发
    }
void GatewayServer::checkLowBatteryAndCharge(const AgvSessionPtr& session,
                                             const TcpConnectionPtr& conn) {
    double battery = session->getBatteryLevel();===
    AgvSession::State state = session->getState();
     GatewayServer::sendProtobufMessage(const TcpConnectionPtr& conn,
    // 触发条件：电量 < 20% 且未在充电              uint16_t msg_type,
    if (battery < kLowBatteryThreshold && state != AgvSession::CHARGING) {essage) {
        LOG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
                 << "] LOW BATTERY (" << battery << "%), sending charge command";
        !message.SerializeToString(&payload)) {
        sendChargeCommand(session->getAgvId(), conn);ssage";
        session->setState(AgvSession::CHARGING);  // 标记为充电中，避免重复下发
    }
}   
    // 2. 使用 LengthHeaderCodec 编码
// ==================== 消息发送实现 ====================
    if (!LengthHeaderCodec::encode(&buf, msg_type, payload)) {
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

// ==================== 上行看门狗实现 ====================
        // 超时检测
void GatewayServer::onWatchdogTimer() {tMs && session->getState() == AgvSession::ONLINE) {
    Timestamp now = Timestamp::now();ion::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
    MutexLockGuard lock(sessions_mutex_);s > " << kSessionTimeoutMs << "ms)";
    for (auto& pair : sessions_) {
        const std::string& agv_id = pair.first;
        AgvSessionPtr session = pair.second;
        
        // 计算距离最后活跃时间的间隔（毫秒）引擎实现 ====================
        double elapsed_sec = timeDifference(now, session->getLastActiveTime());
        int64_t elapsed_ms = static_cast<int64_t>(elapsed_sec * 1000);ion,
                                             const TcpConnectionPtr& conn) {
        // 超时检测ery = session->getBatteryLevel();
        if (elapsed_ms > kSessionTimeoutMs && session->getState() == AgvSession::ONLINE) {
            session->setState(AgvSession::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
                      << elapsed_ms << "ms > " << kSessionTimeoutMs << "ms)";
        }OG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
    }            << "] LOW BATTERY (" << battery << "%), sending charge command";
}       
        sendChargeCommand(session->getAgvId(), conn);
// ==================== 基础业务引擎实现 ====================标记为充电中，避免重复下发
    }
void GatewayServer::checkLowBatteryAndCharge(const AgvSessionPtr& session,
                                             const TcpConnectionPtr& conn) {
    double battery = session->getBatteryLevel();===
    AgvSession::State state = session->getState();
     GatewayServer::sendProtobufMessage(const TcpConnectionPtr& conn,
    // 触发条件：电量 < 20% 且未在充电              uint16_t msg_type,
    if (battery < kLowBatteryThreshold && state != AgvSession::CHARGING) {essage) {
        LOG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
                 << "] LOW BATTERY (" << battery << "%), sending charge command";
        !message.SerializeToString(&payload)) {
        sendChargeCommand(session->getAgvId(), conn);ssage";
        session->setState(AgvSession::CHARGING);  // 标记为充电中，避免重复下发
    }
}   
    // 2. 使用 LengthHeaderCodec 编码
// ==================== 消息发送实现 ====================
    if (!LengthHeaderCodec::encode(&buf, msg_type, payload)) {
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

// ==================== 上行看门狗实现 ====================
        // 超时检测
void GatewayServer::onWatchdogTimer() {tMs && session->getState() == AgvSession::ONLINE) {
    Timestamp now = Timestamp::now();ion::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
    MutexLockGuard lock(sessions_mutex_);s > " << kSessionTimeoutMs << "ms)";
    for (auto& pair : sessions_) {
        const std::string& agv_id = pair.first;
        AgvSessionPtr session = pair.second;
        
        // 计算距离最后活跃时间的间隔（毫秒）引擎实现 ====================
        double elapsed_sec = timeDifference(now, session->getLastActiveTime());
        int64_t elapsed_ms = static_cast<int64_t>(elapsed_sec * 1000);ion,
                                             const TcpConnectionPtr& conn) {
        // 超时检测ery = session->getBatteryLevel();
        if (elapsed_ms > kSessionTimeoutMs && session->getState() == AgvSession::ONLINE) {
            session->setState(AgvSession::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
                      << elapsed_ms << "ms > " << kSessionTimeoutMs << "ms)";
        }OG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
    }            << "] LOW BATTERY (" << battery << "%), sending charge command";
}       
        sendChargeCommand(session->getAgvId(), conn);
// ==================== 基础业务引擎实现 ====================标记为充电中，避免重复下发
    }
void GatewayServer::checkLowBatteryAndCharge(const AgvSessionPtr& session,
                                             const TcpConnectionPtr& conn) {
    double battery = session->getBatteryLevel();===
    AgvSession::State state = session->getState();
     GatewayServer::sendProtobufMessage(const TcpConnectionPtr& conn,
    // 触发条件：电量 < 20% 且未在充电              uint16_t msg_type,
    if (battery < kLowBatteryThreshold && state != AgvSession::CHARGING) {essage) {
        LOG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
                 << "] LOW BATTERY (" << battery << "%), sending charge command";
        !message.SerializeToString(&payload)) {
        sendChargeCommand(session->getAgvId(), conn);ssage";
        session->setState(AgvSession::CHARGING);  // 标记为充电中，避免重复下发
    }
}   
    // 2. 使用 LengthHeaderCodec 编码
// ==================== 消息发送实现 ====================
    if (!LengthHeaderCodec::encode(&buf, msg_type, payload)) {
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

// ==================== 上行看门狗实现 ====================
        // 超时检测
void GatewayServer::onWatchdogTimer() {tMs && session->getState() == AgvSession::ONLINE) {
    Timestamp now = Timestamp::now();ion::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
    MutexLockGuard lock(sessions_mutex_);s > " << kSessionTimeoutMs << "ms)";
    for (auto& pair : sessions_) {
        const std::string& agv_id = pair.first;
        AgvSessionPtr session = pair.second;
        
        // 计算距离最后活跃时间的间隔（毫秒）引擎实现 ====================
        double elapsed_sec = timeDifference(now, session->getLastActiveTime());
        int64_t elapsed_ms = static_cast<int64_t>(elapsed_sec * 1000);ion,
                                             const TcpConnectionPtr& conn) {
        // 超时检测ery = session->getBatteryLevel();
        if (elapsed_ms > kSessionTimeoutMs && session->getState() == AgvSession::ONLINE) {
            session->setState(AgvSession::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
                      << elapsed_ms << "ms > " << kSessionTimeoutMs << "ms)";
        }OG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
    }            << "] LOW BATTERY (" << battery << "%), sending charge command";
}       
        sendChargeCommand(session->getAgvId(), conn);
// ==================== 基础业务引擎实现 ====================标记为充电中，避免重复下发
    }
void GatewayServer::checkLowBatteryAndCharge(const AgvSessionPtr& session,
                                             const TcpConnectionPtr& conn) {
    double battery = session->getBatteryLevel();===
    AgvSession::State state = session->getState();
     GatewayServer::sendProtobufMessage(const TcpConnectionPtr& conn,
    // 触发条件：电量 < 20% 且未在充电              uint16_t msg_type,
    if (battery < kLowBatteryThreshold && state != AgvSession::CHARGING) {essage) {
        LOG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
                 << "] LOW BATTERY (" << battery << "%), sending charge command";
        !message.SerializeToString(&payload)) {
        sendChargeCommand(session->getAgvId(), conn);ssage";
        session->setState(AgvSession::CHARGING);  // 标记为充电中，避免重复下发
    }
}   
    // 2. 使用 LengthHeaderCodec 编码
// ==================== 消息发送实现 ====================
    if (!LengthHeaderCodec::encode(&buf, msg_type, payload)) {
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

// ==================== 上行看门狗实现 ====================
        // 超时检测
void GatewayServer::onWatchdogTimer() {tMs && session->getState() == AgvSession::ONLINE) {
    Timestamp now = Timestamp::now();ion::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
    MutexLockGuard lock(sessions_mutex_);s > " << kSessionTimeoutMs << "ms)";
    for (auto& pair : sessions_) {
        const std::string& agv_id = pair.first;
        AgvSessionPtr session = pair.second;
        
        // 计算距离最后活跃时间的间隔（毫秒）引擎实现 ====================
        double elapsed_sec = timeDifference(now, session->getLastActiveTime());
        int64_t elapsed_ms = static_cast<int64_t>(elapsed_sec * 1000);ion,
                                             const TcpConnectionPtr& conn) {
        // 超时检测ery = session->getBatteryLevel();
        if (elapsed_ms > kSessionTimeoutMs && session->getState() == AgvSession::ONLINE) {
            session->setState(AgvSession::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
                      << elapsed_ms << "ms > " << kSessionTimeoutMs << "ms)";
        }OG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
    }            << "] LOW BATTERY (" << battery << "%), sending charge command";
}       
        sendChargeCommand(session->getAgvId(), conn);
// ==================== 基础业务引擎实现 ====================标记为充电中，避免重复下发
    }
void GatewayServer::checkLowBatteryAndCharge(const AgvSessionPtr& session,
                                             const TcpConnectionPtr& conn) {
    double battery = session->getBatteryLevel();===
    AgvSession::State state = session->getState();
     GatewayServer::sendProtobufMessage(const TcpConnectionPtr& conn,
    // 触发条件：电量 < 20% 且未在充电              uint16_t msg_type,
    if (battery < kLowBatteryThreshold && state != AgvSession::CHARGING) {essage) {
        LOG_WARN << "[BUSINESS ENGINE] AGV [" << session->getAgvId()
                 << "] LOW BATTERY (" << battery << "%), sending charge command";
        !message.SerializeToString(&payload)) {
        sendChargeCommand(session->getAgvId(), conn);ssage";
        session->setState(AgvSession::CHARGING);  // 标记为充电中，避免重复下发
    }
}   
    // 2. 使用 LengthHeaderCodec 编码
// ==================== 消息发送实现 ====================
    if (!LengthHeaderCodec::encode(&buf, msg_type, payload)) {
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

// ==================== 上行看门狗实现 ====================
        // 超时检测
void GatewayServer::onWatchdogTimer() {tMs && session->getState() == AgvSession::ONLINE) {
    Timestamp now = Timestamp::now();ion::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="