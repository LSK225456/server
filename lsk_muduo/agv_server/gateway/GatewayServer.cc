#include "GatewayServer.h"
#include "../../muduo/base/Logger.h"
#include "../codec/LengthHeaderCodec.h"
#include "../../muduo/net/TimerId.h"
#include <unistd.h>  // usleep

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
                             const std::string& name,
                             double session_timeout_sec,
                             int worker_threads)
    : loop_(loop),
      server_(loop, listen_addr, name),
      session_timeout_ms_(static_cast<int>(session_timeout_sec * 1000)),
      worker_pool_(new ThreadPool("WorkerPool")) {
    
    // 注册 TcpServer 回调
    server_.setConnectionCallback(
        std::bind(&GatewayServer::onConnection, this, std::placeholders::_1));
    server_.setMessageCallback(
        std::bind(&GatewayServer::onMessage, this,
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    
    // 初始化消息分发器（注册所有消息类型的 handler）
    initDispatcher();
    
    // 启动 Worker 线程池（迭代三新增）
    if (worker_threads > 0) {
        worker_pool_->start(worker_threads);
        LOG_INFO << "Worker thread pool started with " << worker_threads << " threads";
    } else {
        LOG_WARN << "Worker thread pool disabled (worker_threads=0)";
    }
    
    LOG_INFO << "GatewayServer created: " << name 
             << " (session_timeout=" << session_timeout_sec << "s, worker_threads=" << worker_threads << ")";
}

GatewayServer::~GatewayServer() {
    // 停止 Worker 线程池
    if (worker_pool_) {
        worker_pool_->stop();
        LOG_INFO << "Worker thread pool stopped";
    }
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
             << "ms, timeout: " << session_timeout_ms_ << "ms)";
}

// ==================== 消息分发器初始化 ====================

void GatewayServer::initDispatcher() {
    // 注册遥测消息处理器
    dispatcher_.registerHandler<proto::AgvTelemetry>(
        proto::MSG_AGV_TELEMETRY,
        std::bind(&GatewayServer::handleTelemetry, this,
                  std::placeholders::_1, std::placeholders::_2));
    
    // 注册心跳消息处理器
    dispatcher_.registerHandler<proto::Heartbeat>(
        proto::MSG_HEARTBEAT,
        std::bind(&GatewayServer::handleHeartbeat, this,
                  std::placeholders::_1, std::placeholders::_2));
    
    // 【迭代三新增】注册导航任务处理器
    dispatcher_.registerHandler<proto::NavigationTask>(
        proto::MSG_NAVIGATION_TASK,
        std::bind(&GatewayServer::handleNavigationTask, this,
                  std::placeholders::_1, std::placeholders::_2));
    
    // 设置默认回调（处理未注册的消息类型）
    dispatcher_.setDefaultCallback(
        [](const TcpConnectionPtr& conn, uint16_t msg_type,
           const char* /*payload*/, size_t /*len*/) {
            LOG_WARN << "Unknown message type: 0x" << std::hex << msg_type 
                     << std::dec << " from " << conn->peerAddress().toIpPort();
        });
    
    LOG_INFO << "ProtobufDispatcher initialized with " 
             << dispatcher_.handlerCount() << " handlers";
}

// ==================== TcpServer 回调实现 ====================

void GatewayServer::onConnection(const TcpConnectionPtr& conn) {
    if (conn->connected()) {
        LOG_INFO << "New connection: " << conn->peerAddress().toIpPort();
        // 注意：此时尚未收到 agv_id，会话注册延迟到首个消息到达
    } else {
        LOG_INFO << "Connection closed: " << conn->peerAddress().toIpPort();
        
        // 使用 ConcurrentMap 清理断开的连接对应的会话
        removeSessionByConnection(conn);
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
        
        // 使用 ProtobufDispatcher 分发消息（替换原 switch-case）
        dispatcher_.dispatch(conn, msg_type, payload.data(), payload.size());
    }
}

// ==================== 业务处理实现 ====================

void GatewayServer::handleTelemetry(const TcpConnectionPtr& conn,
                                    const proto::AgvTelemetry& msg) {
    const std::string& agv_id = msg.agv_id();
    
    // 1. 查找或创建会话,懒加载注册,第一次收到消息时，才建立 Session
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
    AgvSessionPtr session = sessionManager_.findSession(agv_id);
    if (!session) {
        registerSession(agv_id, conn);
        session = sessionManager_.findSession(agv_id);
    }
    
    if (!session) {
        LOG_ERROR << "Failed to create session for AGV [" << agv_id << "]";
        return;
    }
    
    // 刷新活跃时间
    session->updateActiveTime();
    LOG_DEBUG << "Heartbeat from [" << agv_id << "]";
    
    // 回复心跳（迭代一：Day 3-4 客户端看门狗需求）
    proto::Heartbeat response;
    response.set_agv_id(agv_id);
    response.set_timestamp(Timestamp::now().microSecondsSinceEpoch());
    sendProtobufMessage(conn, proto::MSG_HEARTBEAT, response);
    
    LOG_DEBUG << "[SEND] Heartbeat response to [" << agv_id << "]";
}

// ==================== 会话管理实现（SessionManager）====================

void GatewayServer::registerSession(const std::string& agv_id,
                                    const TcpConnectionPtr& conn) {
    // 委托给 SessionManager
    sessionManager_.registerSession(agv_id, conn);
}

void GatewayServer::removeSession(const std::string& agv_id) {
    // 委托给 SessionManager
    sessionManager_.removeSession(agv_id);
}

AgvSessionPtr GatewayServer::findSession(const std::string& agv_id) {
    // 委托给 SessionManager
    return sessionManager_.findSession(agv_id);
}

void GatewayServer::removeSessionByConnection(const TcpConnectionPtr& conn) {
    // 委托给 SessionManager
    sessionManager_.removeSessionByConnection(conn);
}

// ==================== 上行看门狗实现 ====================

void GatewayServer::onWatchdogTimer() {
    Timestamp now = Timestamp::now();
    
    // 使用 SessionManager::forEach 安全遍历（读锁保护）
    sessionManager_.forEach([&now, this](const std::string& agv_id,
                                          const AgvSessionPtr& session) {
        // 计算距离最后活跃时间的间隔（毫秒）
        double elapsed_sec = timeDifference(now, session->getLastActiveTime());
        int64_t elapsed_ms = static_cast<int64_t>(elapsed_sec * 1000);
        
        // 超时检测
        if (elapsed_ms > session_timeout_ms_ && session->getState() == AgvSession::ONLINE) {
            session->setState(AgvSession::OFFLINE);
            LOG_ERROR << "[WATCHDOG ALARM] AGV [" << agv_id << "] OFFLINE (timeout="
                      << elapsed_ms << "ms > " << session_timeout_ms_ << "ms)";
        }
    });
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
    conn->send(buf.retrieveAllAsString());
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

// ==================== Worker 线程任务处理【迭代三新增】====================

void GatewayServer::handleNavigationTask(const TcpConnectionPtr& conn,
                                         const proto::NavigationTask& msg) {
    const std::string& agv_id = msg.target_agv_id();
    
    LOG_INFO << "[IO THREAD] Received NavigationTask for [" << agv_id 
             << "] task_id=" << msg.task_id() << ", submitting to Worker";
    
    // 1. 查找会话
    AgvSessionPtr session = findSession(agv_id);
    if (!session) {
        LOG_WARN << "Session not found for AGV [" << agv_id << "], creating new";
        registerSession(agv_id, conn);
        session = findSession(agv_id);
    }
    
    if (!session) {
        LOG_ERROR << "Failed to create session for AGV [" << agv_id << "]";
        return;
    }
    
    // 2. 构造 WorkerTask（强类型，无需重新序列化）
    auto task = std::make_shared<WorkerTask>(
        conn,  // TcpConnectionPtr -> TcpConnectionWeakPtr 自动转换
        session,
        std::make_shared<proto::NavigationTask>(msg),  // 拷贝消息到 shared_ptr
        proto::MSG_NAVIGATION_TASK
    );
    
    // 3. 投递到 Worker 线程池（避免阻塞 IO 线程）
    worker_pool_->run([task, this]() {
        this->processWorkerTask(task);
    });
    
    LOG_DEBUG << "[IO THREAD] Task submitted, queue_latency=" 
              << task->getQueueLatencyMs() << "ms";
}

void GatewayServer::processWorkerTask(const std::shared_ptr<WorkerTask>& task) {
    LOG_INFO << "[WORKER THREAD] Processing task (type=0x" << std::hex << task->msg_type 
             << std::dec << ", queue_latency=" << task->getQueueLatencyMs() << "ms)";
    
    // 1. 检查连接有效性（弱引用提升）
    auto conn = task->getConnection();
    if (!conn) {
        LOG_WARN << "[WORKER THREAD] Connection closed, task cancelled";
        return;
    }
    
    // 2. 类型安全的消息提取
    auto nav_task = task->getMessage<proto::NavigationTask>();
    if (!nav_task) {
        LOG_ERROR << "[WORKER THREAD] Failed to cast message to NavigationTask";
        return;
    }
    
    // 3. 模拟数据库写入操作（阻塞 50ms）
    simulateDatabaseWrite(*nav_task);
    
    // 4. 通过 runInLoop 回到 IO 线程发送响应
    loop_->runInLoop([conn, nav_task, this]() {
        // 构造响应消息
        proto::CommonResponse response;
        response.set_status(proto::STATUS_OK);
        response.set_message("NavigationTask accepted");
        response.set_timestamp(Timestamp::now().microSecondsSinceEpoch());
        
        // 发送响应
        sendProtobufMessage(conn, proto::MSG_COMMON_RESPONSE, response);
        
        LOG_INFO << "[IO THREAD] Response sent for task_id=" << nav_task->task_id();
    });
}

void GatewayServer::simulateDatabaseWrite(const proto::NavigationTask& msg) {
    LOG_INFO << "[WORKER THREAD] Simulating database write for task_id=" << msg.task_id();
    LOG_INFO << "  Target AGV: " << msg.target_agv_id();
    LOG_INFO << "  Target Point: (" << msg.target_node().x() << ", " 
             << msg.target_node().y() << ")";
    LOG_INFO << "  Operation: " << msg.operation();
    LOG_INFO << "  Path Points: " << msg.global_path_size();
    
    // 模拟数据库写入延迟（50ms = 50000 微秒）
    // 替代原文档中的"A*路径规划"，改为"存储到 MySQL/InfluxDB"
    usleep(50000);  // 50ms
    
    LOG_INFO << "[WORKER THREAD] Database write completed (simulated 50ms)";
}

}  // namespace gateway
}  // namespace agv