#ifndef LSK_MUDUO_TEST_MOCK_AGV_CLIENT_H
#define LSK_MUDUO_TEST_MOCK_AGV_CLIENT_H

#include "../muduo/net/TcpClient.h"
#include "../muduo/net/EventLoop.h"
#include "../muduo/net/InetAddress.h"
#include "../muduo/net/Buffer.h"
#include "../muduo/net/TimerId.h"
#include "../muduo/base/Timestamp.h"
#include "../muduo/base/Logger.h"
#include "../agv_server/proto/message.pb.h"
#include "../agv_server/proto/common.pb.h"
#include "../agv_server/proto/message_id.h"
#include "../agv_server/codec/LengthHeaderCodec.h"
#include <string>
#include <memory>
#include <atomic>
#include <functional>

/**
 * @brief 模拟 AGV 客户端（迭代一：Day 3-4 智能模拟器）
 * 
 * @note 核心特性：
 *       1. 物理仿真：电量模拟（IDLE -0.5%/s, MOVING -1%/s, CHARGING +2%/s）
 *       2. 状态机：IDLE, MOVING, E_STOP, MOVING_TO_CHARGER, CHARGING
 *       3. 下行看门狗：1秒未收到服务器消息 -> E_STOP
 *       4. 指令响应：AgvCommand -> 状态切换
 *       5. 自动发送：Telemetry(50Hz), Heartbeat(500ms)
 * 
 * @note 设计目标：
 *       - 从"只会发包的哑巴"升级为"能模拟物理特性的虚拟车"
 *       - 为后续压测模拟器（LoadTester）提供基础组件
 */
class MockAgvClient {
public:
    // ==================== 车辆状态枚举 ====================
    
    /**
     * @brief 车辆运行状态
     */
    enum State {
        IDLE,                ///< 空闲（正常耗电 -0.5%/s）
        MOVING,              ///< 移动中（高速耗电 -1%/s）
        E_STOP,              ///< 紧急停止（服务器失联或主动急停，不耗电）
        MOVING_TO_CHARGER,   ///< 前往充电点（移动中，-1%/s）
        CHARGING             ///< 充电中（+2%/s，充到100%后等待RESUME）
    };

    // ==================== 构造与生命周期 ====================
    
    /**
     * @brief 构造函数
     * @param loop 事件循环（必须在 IO 线程创建）
     * @param server_addr 服务器地址
     * @param agv_id 车辆唯一标识符
     * @param telemetry_freq 遥测发送频率（Hz），默认 50Hz
     * @param initial_battery 初始电量（%），默认 100.0，用于快速测试低电量场景
     * @param watchdog_timeout 看门狗超时时间（秒），默认 5.0s（迭代二要求）
     * 
     * @note 构造后调用 connect() 连接服务器
     */
    MockAgvClient(lsk_muduo::EventLoop* loop,
                  const lsk_muduo::InetAddress& server_addr,
                  const std::string& agv_id,
                  double telemetry_freq = 50.0,
                  double initial_battery = 100.0,
                  double watchdog_timeout = 5.0);
    
    ~MockAgvClient();

    // ==================== 连接管理 ====================
    
    /**
     * @brief 主动连接服务器
     */
    void connect();

    /**
     * @brief 主动断开连接
     */
    void disconnect();

    // ==================== 状态查询 ====================
    
    /**
     * @brief 获取当前状态
     */
    State getState() const { return state_; }

    /**
     * @brief 获取当前电量
     */
    double getBattery() const { return battery_; }

    /**
     * @brief 获取车辆 ID
     */
    std::string getAgvId() const { return agv_id_; }

    /**
     * @brief 是否已连接
     */
    bool isConnected() const { return connected_; }

    // ==================== 状态字符串工具 ====================
    
    static const char* stateToString(State state);

private:
    // ==================== TcpClient 回调 ====================
    
    /**
     * @brief 连接建立/断开回调
     */
    void onConnection(const lsk_muduo::TcpConnectionPtr& conn);

    /**
     * @brief 消息到达回调
     */
    void onMessage(const lsk_muduo::TcpConnectionPtr& conn,
                   lsk_muduo::Buffer* buf,
                   lsk_muduo::Timestamp receive_time);

    // ==================== 消息处理 ====================
    
    /**
     * @brief 处理下行消息（解析包头后分发）
     */
    void handleProtobufMessage(uint16_t msg_type, const char* payload, size_t len);

    /**
     * @brief 处理 AgvCommand 指令
     */
    void handleAgvCommand(const agv::proto::AgvCommand& cmd);

    /**
     * @brief 处理 Heartbeat 响应
     */
    void handleHeartbeat(const agv::proto::Heartbeat& msg);

    /**
     * @brief 处理 NavigationTask【迭代三新增】
     * @note 模拟执行导航任务（3秒后完成）
     */
    void handleNavigationTask(const agv::proto::NavigationTask& task);

    // ==================== 定时任务 ====================
    
    /**
     * @brief 遥测定时器回调（50Hz）
     * @note 高频任务，发送 AgvTelemetry
     */
    void onTelemetryTimer();

    /**
     * @brief 心跳定时器回调（500ms）
     * @note 中频任务，发送 Heartbeat
     */
    void onHeartbeatTimer();

    /**
     * @brief 电量更新定时器回调（1秒）
     * @note 低频任务，根据状态更新电量
     */
    void onBatteryTimer();

    /**
     * @brief 下行看门狗定时器回调（100ms）
     * @note 检查是否 1 秒未收到服务器消息
     */
    void onWatchdogTimer();

    // ==================== 状态切换与业务逻辑 ====================
    
    /**
     * @brief 切换车辆状态
     * @param new_state 新状态
     * 
     * @note 打印状态切换日志
     */
    void setState(State new_state);

    /**
     * @brief 更新电量（限制在 0-100%）
     * @param delta 变化量（可正可负）
     */
    void updateBattery(double delta);

    /**
     * @brief 刷新服务器消息时间戳（收到任何下行消息时调用）
     */
    void refreshServerMessageTime();

    /**
     * @brief 充电完成后的逻辑（电量达到 100%）
     * @note 保持 CHARGING 状态，等待服务器下发 RESUME 指令
     */
    void onChargingComplete();

    /**
     * @brief 模拟前往充电点（MOVING_TO_CHARGER -> CHARGING）
     * @note 延迟 3 秒后自动切换到 CHARGING 状态
     */
    void startMovingToCharger();

    // ==================== 消息发送 ====================
    
    /**
     * @brief 发送 Protobuf 消息（通用接口）
     */
    void sendProtobufMessage(uint16_t msg_type, 
                             const google::protobuf::Message& message);

    /**
     * @brief 发送遥测数据
     */
    void sendTelemetry();

    /**
     * @brief 发送心跳
     */
    void sendHeartbeat();

private:
    // ==================== 成员变量 ====================
    
    // 网络相关
    lsk_muduo::EventLoop* loop_;
    lsk_muduo::TcpClient client_;
    std::string agv_id_;
    std::atomic<bool> connected_;
    lsk_muduo::TcpConnectionPtr conn_;

    // 物理状态
    std::atomic<State> state_;
    std::atomic<double> battery_;
    double x_;                   ///< X 坐标（米）
    double y_;                   ///< Y 坐标（米）
    double theta_;               ///< 航向角（度）

    // 定时器配置
    double telemetry_freq_;      ///< 遥测频率（Hz）
    double telemetry_interval_;  ///< 遥测间隔（秒）

    // 看门狗
    lsk_muduo::Timestamp last_server_msg_time_;  ///< 最后收到服务器消息的时间
    double watchdog_timeout_sec_;                ///< 看门狗超时（秒），可配置

    // 常量配置
    static constexpr double kHeartbeatIntervalSec = 0.5;      ///< 心跳间隔（秒）
    static constexpr double kBatteryUpdateIntervalSec = 1.0;  ///< 电量更新间隔（秒）
    static constexpr double kWatchdogCheckIntervalSec = 0.1;  ///< 看门狗检查间隔（秒）
    static constexpr double kMovingToChargerDelaySec = 3.0;   ///< 移动到充电点的延迟（秒）

    // 电量变化率（%/秒）
    static constexpr double kBatteryDrainIdle = -0.5;         ///< IDLE 耗电率
    static constexpr double kBatteryDrainMoving = -1.0;       ///< MOVING 耗电率
    static constexpr double kBatteryChargeRate = 2.0;         ///< 充电速率

    // 电量阈值
    static constexpr double kBatteryMin = 0.0;
    static constexpr double kBatteryMax = 100.0;
};

#endif  // LSK_MUDUO_TEST_MOCK_AGV_CLIENT_H
