#include "MockAgvClient.h"
#include <iostream>

using namespace lsk_muduo;
using namespace agv::proto;
using namespace agv::codec;

// ==================== 构造与生命周期 ====================

MockAgvClient::MockAgvClient(EventLoop* loop,
                             const InetAddress& server_addr,
                             const std::string& agv_id,
                             double telemetry_freq,
                             double initial_battery,
                             double watchdog_timeout)
    : loop_(loop),
      client_(loop, server_addr, "MockAGV-" + agv_id),
      agv_id_(agv_id),
      connected_(false),
      state_(IDLE),
      battery_(initial_battery),
      x_(0.0),
      y_(0.0),
      theta_(0.0),
      telemetry_freq_(telemetry_freq),
      telemetry_interval_(1.0 / telemetry_freq),
      last_server_msg_time_(Timestamp::now()),
      watchdog_timeout_sec_(watchdog_timeout) {
    
    // 设置 TcpClient 回调
    client_.setConnectionCallback(
        std::bind(&MockAgvClient::onConnection, this, std::placeholders::_1));
    client_.setMessageCallback(
        std::bind(&MockAgvClient::onMessage, this,
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    
    // LOG_INFO << "[MockAGV-" << agv_id_ << "] Created (freq=" << telemetry_freq
    //          << "Hz, watchdog_timeout=" << watchdog_timeout << "s)";
}

MockAgvClient::~MockAgvClient() {
    // LOG_INFO << "[MockAGV-" << agv_id_ << "] Destroyed";
}

// ==================== 连接管理 ====================

void MockAgvClient::connect() {
    // LOG_INFO << "[MockAGV-" << agv_id_ << "] Connecting to server...";
    client_.connect();
}

void MockAgvClient::disconnect() {
    // LOG_INFO << "[MockAGV-" << agv_id_ << "] Disconnecting...";
    client_.disconnect();
}

// ==================== TcpClient 回调实现 ====================

void MockAgvClient::onConnection(const TcpConnectionPtr& conn) {
    if (conn->connected()) {
        connected_ = true;
        conn_ = conn;
        LOG_WARN << "[✅ CONNECTED] AGV [" << agv_id_ << "] -> "
                 << conn->peerAddress().toIpPort();
        
        // 刷新服务器消息时间戳（连接成功视为收到消息）
        refreshServerMessageTime();
        
        // 启动定时器
        loop_->runEvery(telemetry_interval_, 
                        std::bind(&MockAgvClient::onTelemetryTimer, this));
        loop_->runEvery(kHeartbeatIntervalSec, 
                        std::bind(&MockAgvClient::onHeartbeatTimer, this));
        loop_->runEvery(kBatteryUpdateIntervalSec, 
                        std::bind(&MockAgvClient::onBatteryTimer, this));
        loop_->runEvery(kWatchdogCheckIntervalSec, 
                        std::bind(&MockAgvClient::onWatchdogTimer, this));
        
        // LOG_INFO << "[MockAGV-" << agv_id_ << "] Timers started";
        
    } else {
        connected_ = false;
        LOG_WARN << "[❌ DISCONNECTED] AGV [" << agv_id_ << "]";
        
        // 触发紧急停止（连接断开）
        if (state_ != E_STOP) {
            setState(E_STOP);
            LOG_ERROR << "[⚠️  EMERGENCY] AGV [" << agv_id_ << "] Server Lost!";
        }
    }
}

void MockAgvClient::onMessage(const TcpConnectionPtr& conn,
                              Buffer* buf,
                              Timestamp receive_time) {
    (void)conn;
    (void)receive_time;
    
    // 刷新看门狗（收到任何消息）
    refreshServerMessageTime();
    
    // 循环处理粘包：一个 Buffer 可能包含多个完整消息
    while (LengthHeaderCodec::hasCompleteMessage(buf)) {
        uint16_t msg_type = 0;
        uint16_t flags = 0;
        std::string payload;
        
        // 使用 LengthHeaderCodec 解码
        if (!LengthHeaderCodec::decode(buf, &msg_type, &payload, &flags)) {
            LOG_ERROR << "[MockAGV-" << agv_id_ << "] Failed to decode message";
            return;
        }
        
        // 处理 Protobuf 消息
        handleProtobufMessage(msg_type, payload.data(), payload.size());
    }
}

// ==================== 消息处理实现 ====================

void MockAgvClient::handleProtobufMessage(uint16_t msg_type, 
                                          const char* payload, 
                                          size_t len) {
    switch (msg_type) {
        case MSG_AGV_COMMAND: {
            AgvCommand cmd;
            if (!cmd.ParseFromArray(payload, static_cast<int>(len))) {
                LOG_ERROR << "[MockAGV-" << agv_id_ << "] Failed to parse AgvCommand";
                return;
            }
            handleAgvCommand(cmd);
            break;
        }
        case MSG_HEARTBEAT: {
            Heartbeat msg;
            if (!msg.ParseFromArray(payload, static_cast<int>(len))) {
                LOG_ERROR << "[MockAGV-" << agv_id_ << "] Failed to parse Heartbeat";
                return;
            }
            handleHeartbeat(msg);
            break;
        }
        case MSG_NAVIGATION_TASK: {  // 【迭代三新增】
            NavigationTask task;
            if (!task.ParseFromArray(payload, static_cast<int>(len))) {
                LOG_ERROR << "[MockAGV-" << agv_id_ << "] Failed to parse NavigationTask";
                return;
            }
            handleNavigationTask(task);
            break;
        }
        case MSG_LATENCY_PROBE: {  // 【迭代三第6周新增】
            LatencyProbe probe;
            if (!probe.ParseFromArray(payload, static_cast<int>(len))) {
                LOG_ERROR << "[MockAGV-" << agv_id_ << "] Failed to parse LatencyProbe";
                return;
            }
            handleLatencyProbe(probe);
            break;
        }
        default:
            LOG_WARN << "[MockAGV-" << agv_id_ << "] Unknown message type: 0x" 
                     << std::hex << msg_type << std::dec;
            break;
    }
}

void MockAgvClient::handleAgvCommand(const AgvCommand& cmd) {
    // LOG_INFO << "[MockAGV-" << agv_id_ << "] [RECV] AgvCommand: cmd_type="
    //          << cmd.cmd_type() << " (" << CommandType_Name(cmd.cmd_type()) << ")";
    
    switch (cmd.cmd_type()) {
        case CMD_EMERGENCY_STOP:
            LOG_WARN << "[⚠️  E-STOP] AGV [" << agv_id_ << "] received EMERGENCY_STOP";
            setState(E_STOP);
            break;
            
        case CMD_RESUME:
            // LOG_INFO << "[MockAGV-" << agv_id_ << "] Receiving RESUME command";
            if (state_ == E_STOP || state_ == CHARGING) {
                setState(IDLE);
            }
            break;
            
        case CMD_PAUSE:
            // LOG_INFO << "[MockAGV-" << agv_id_ << "] Receiving PAUSE command";
            if (state_ == MOVING || state_ == MOVING_TO_CHARGER) {
                setState(IDLE);
            }
            break;
            
        case CMD_REBOOT:
            // LOG_WARN << "[MockAGV-" << agv_id_ << "] Receiving REBOOT command (ignored)";
            break;
            
        case CMD_NAVIGATE_TO:
            // LOG_INFO << "[MockAGV-" << agv_id_ << "] Receiving NAVIGATE_TO command";
            if (battery_ < 20.0) {
                // 低电量时收到 NAVIGATE_TO，解释为充电指令
                LOG_WARN << "[🔋 CHARGING] AGV [" << agv_id_ 
                         << "] battery=" << battery_ << "%, interpreting as charge command";
                startMovingToCharger();
            } else {
                setState(MOVING);
            }
            break;
            
        default:
            LOG_WARN << "[MockAGV-" << agv_id_ << "] Unknown command type: " 
                     << cmd.cmd_type();
            break;
    }
}

void MockAgvClient::handleHeartbeat(const Heartbeat& msg) {
    (void)msg;
    LOG_DEBUG << "[MockAGV-" << agv_id_ << "] [RECV] Heartbeat response from server";
    // 看门狗已在 onMessage 中刷新，无需额外处理
}

void MockAgvClient::handleNavigationTask(const NavigationTask& task) {
    // LOG_INFO << "[MockAGV-" << agv_id_ << "] [RECV] NavigationTask: task_id=" << task.task_id();
    // LOG_INFO << "  Target: (" << task.target_node().x() << ", " << task.target_node().y() << ")";
    // LOG_INFO << "  Operation: " << OperationType_Name(task.operation());
    // LOG_INFO << "  Path points: " << task.global_path_size();
    
    // 模拟执行导航任务：
    // 1. 切换到 MOVING 状态
    // 2. 3秒后切换回 IDLE（模拟任务完成）
    setState(MOVING);
    
    // 延迟任务：3秒后恢复到 IDLE
    loop_->runAfter(3.0, [this, task_id = task.task_id()]() {
        if (state_ == MOVING) {
            // LOG_INFO << "[MockAGV-" << agv_id_ << "] NavigationTask completed: task_id=" << task_id;
            setState(IDLE);
            
            // 可选：发送 TaskFeedback（迭代三后续完善）
            // TaskFeedback feedback;
            // feedback.set_agv_id(agv_id_);
            // feedback.set_task_id(task_id);
            // feedback.set_status(TASK_COMPLETED);
            // sendProtobufMessage(MSG_TASK_FEEDBACK, feedback);
        }
    });
}

// ==================== 定时任务实现 ====================

void MockAgvClient::onTelemetryTimer() {
    if (!connected_ || state_ == E_STOP) {
        return;  // 未连接或急停状态不发送
    }
    sendTelemetry();
}

void MockAgvClient::onHeartbeatTimer() {
    if (!connected_ || state_ == E_STOP) {
        return;  // 未连接或急停状态不发送
    }
    sendHeartbeat();
}

void MockAgvClient::onBatteryTimer() {
    if (state_ == E_STOP) {
        return;  // 急停状态不更新电量
    }
    
    double delta = 0.0;
    switch (state_) {
        case IDLE:
            delta = kBatteryDrainIdle;  // -0.5%/s
            break;
        case MOVING:
        case MOVING_TO_CHARGER:
            delta = kBatteryDrainMoving;  // -1.0%/s
            break;
        case CHARGING:
            delta = kBatteryChargeRate;  // +2.0%/s
            break;
        case E_STOP:
            delta = 0.0;  // 不变
            break;
    }
    
    updateBattery(delta);
    
    // 充电完成检测
    if (state_ == CHARGING && battery_ >= kBatteryMax) {
        onChargingComplete();
    }
}

void MockAgvClient::onWatchdogTimer() {
    if (!connected_) {
        return;  // 未连接不检查
    }
    
    Timestamp now = Timestamp::now();
    double elapsed_sec = (now.microSecondsSinceEpoch() - 
                         last_server_msg_time_.microSecondsSinceEpoch()) / 1000000.0;
    
    if (elapsed_sec > watchdog_timeout_sec_ && state_ != E_STOP) {
        LOG_ERROR << "[⚠️  WATCHDOG] AGV [" << agv_id_ 
                  << "] Server timeout (" << elapsed_sec << "s)";
        setState(E_STOP);
    }
}

// ==================== 状态切换与业务逻辑实现 ====================

void MockAgvClient::setState(State new_state) {
    if (state_ == new_state) return;
    
    State old_state = state_;
    state_ = new_state;
    
    LOG_WARN << "[🚗 STATE] AGV [" << agv_id_ << "] " 
             << stateToString(old_state) << " → " << stateToString(new_state);
}

void MockAgvClient::updateBattery(double delta) {
    double old_battery = battery_;
    double new_battery = old_battery + delta;
    
    // 限制范围
    if (new_battery < kBatteryMin) new_battery = kBatteryMin;
    if (new_battery > kBatteryMax) new_battery = kBatteryMax;
    
    battery_ = new_battery;
    
    // 只在电量显著变化时输出（每5%）
    int old_level = static_cast<int>(old_battery / 5.0);
    int new_level = static_cast<int>(new_battery / 5.0);
    if (old_level != new_level || new_battery <= 20.0) {
        LOG_WARN << "[🔋 BATTERY] AGV [" << agv_id_ << "] " 
                 << old_battery << "% → " << new_battery << "%";
    }
}

void MockAgvClient::refreshServerMessageTime() {
    last_server_msg_time_ = Timestamp::now();
}

void MockAgvClient::onChargingComplete() {
    LOG_WARN << "[⚡ CHARGED] AGV [" << agv_id_ 
             << "] battery=" << battery_ << "%, waiting for RESUME...";
    // 保持 CHARGING 状态，等待服务器发送 RESUME 指令
}

void MockAgvClient::startMovingToCharger() {
    LOG_WARN << "[🚗 TO CHARGER] AGV [" << agv_id_ 
             << "] ETA=" << kMovingToChargerDelaySec << "s";
    setState(MOVING_TO_CHARGER);
    
    // 模拟移动延迟（3秒后到达充电点）
    loop_->runAfter(kMovingToChargerDelaySec, [this]() {
        if (state_ == MOVING_TO_CHARGER) {
            LOG_WARN << "[⚡ ARRIVED] AGV [" << agv_id_ << "] at charger, start charging";
            setState(CHARGING);
        }
    });
}

// ==================== 消息发送实现 ====================

void MockAgvClient::sendProtobufMessage(uint16_t msg_type, 
                                        const google::protobuf::Message& message) {
    if (!connected_) {
        return;
    }
    
    // 1. 序列化 Protobuf
    std::string payload;
    if (!message.SerializeToString(&payload)) {
        LOG_ERROR << "[MockAGV-" << agv_id_ << "] Failed to serialize message";
        return;
    }
    
    // 2. 使用 LengthHeaderCodec 编码
    Buffer buf;
    if (!LengthHeaderCodec::encode(&buf, msg_type, payload)) {
        LOG_ERROR << "[MockAGV-" << agv_id_ << "] Failed to encode message";
        return;
    }
    
    // 3. 发送
    conn_->send(buf.retrieveAllAsString());
}

void MockAgvClient::sendTelemetry() {
    AgvTelemetry msg;
    msg.set_agv_id(agv_id_);
    msg.set_timestamp(Timestamp::now().microSecondsSinceEpoch());
    msg.set_x(x_);
    msg.set_y(y_);
    msg.set_theta(theta_);
    msg.set_confidence(0.95);
    msg.set_battery(battery_);
    msg.set_linear_velocity(0.0);
    msg.set_angular_velocity(0.0);
    msg.set_acceleration(0.0);
    msg.set_payload_weight(0.0);
    msg.set_error_code(0);
    msg.set_fork_height(0.0);
    
    sendProtobufMessage(MSG_AGV_TELEMETRY, msg);
    
    LOG_DEBUG << "[MockAGV-" << agv_id_ << "] [SEND] Telemetry: battery=" 
              << battery_ << "%, state=" << stateToString(state_);
}

void MockAgvClient::sendHeartbeat() {
    Heartbeat msg;
    msg.set_agv_id(agv_id_);
    msg.set_timestamp(Timestamp::now().microSecondsSinceEpoch());
    
    sendProtobufMessage(MSG_HEARTBEAT, msg);
    
    LOG_DEBUG << "[MockAGV-" << agv_id_ << "] [SEND] Heartbeat";
}

// ==================== LatencyProbe 处理【迭代三第6周新增】====================

void MockAgvClient::handleLatencyProbe(const LatencyProbe& probe) {
    if (!probe.is_response()) {
        // 收到服务器 Ping，回复 Pong
        LOG_DEBUG << "[MockAGV-" << agv_id_ << "] [RECV] LatencyProbe Ping, seq="
                  << probe.seq_num();
        
        LatencyProbe pong;
        pong.set_target_agv_id(agv_id_);
        pong.set_send_timestamp(probe.send_timestamp());  // 保持原始时间戳
        pong.set_seq_num(probe.seq_num());                // 保持原始序列号
        pong.set_is_response(true);
        
        sendProtobufMessage(MSG_LATENCY_PROBE, pong);
        
        LOG_DEBUG << "[MockAGV-" << agv_id_ << "] [SEND] LatencyProbe Pong, seq="
                  << probe.seq_num();
    } else {
        LOG_DEBUG << "[MockAGV-" << agv_id_ << "] [RECV] Unexpected LatencyProbe Pong";
    }
}

// ==================== 工具函数 ====================

const char* MockAgvClient::stateToString(State state) {
    switch (state) {
        case IDLE:                return "IDLE";
        case MOVING:              return "MOVING";
        case E_STOP:              return "E_STOP";
        case MOVING_TO_CHARGER:   return "MOVING_TO_CHARGER";
        case CHARGING:            return "CHARGING";
        default:                  return "UNKNOWN";
    }
}
