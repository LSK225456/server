#pragma once

#include <stdint.h>

namespace agv {
namespace proto {

/**
 * @brief 消息类型 ID 定义
 * @note 用于 LengthHeaderCodec 的 8 字节包头中的 MsgType 字段
 * @note 分配规则：
 *       - 上行消息（车 -> 服务器）：0x1000 - 0x1FFF
 *       - 下行消息（服务器 -> 车）：0x2000 - 0x2FFF
 *       - 通用消息（双向）：        0x3000 - 0x3FFF
 */

// ==================== 上行消息 ID ====================
constexpr uint16_t MSG_AGV_TELEMETRY    = 0x1001;  // AgvTelemetry（高频遥测）
constexpr uint16_t MSG_MPC_TRAJECTORY   = 0x1002;  // MpcTrajectory（预测轨迹）
constexpr uint16_t MSG_TASK_FEEDBACK    = 0x1003;  // TaskFeedback（任务反馈）

// ==================== 下行消息 ID ====================
constexpr uint16_t MSG_AGV_COMMAND      = 0x2001;  // AgvCommand（系统指令）
constexpr uint16_t MSG_NAVIGATION_TASK  = 0x2002;  // NavigationTask（导航任务）
constexpr uint16_t MSG_LATENCY_PROBE    = 0x2003;  // LatencyProbe（延迟探测）

// ==================== 通用消息 ID ====================
constexpr uint16_t MSG_COMMON_RESPONSE  = 0x3001;  // CommonResponse（通用响应）
constexpr uint16_t MSG_HEARTBEAT        = 0x3002;  // Heartbeat（心跳）

// ==================== 辅助函数 ====================

/**
 * @brief 判断消息 ID 是否为上行消息
 */
inline bool isUpstreamMessage(uint16_t msg_id) {
    return (msg_id >= 0x1000 && msg_id < 0x2000);
}

/**
 * @brief 判断消息 ID 是否为下行消息
 */
inline bool isDownstreamMessage(uint16_t msg_id) {
    return (msg_id >= 0x2000 && msg_id < 0x3000);
}

/**
 * @brief 判断消息 ID 是否为通用消息
 */
inline bool isCommonMessage(uint16_t msg_id) {
    return (msg_id >= 0x3000 && msg_id < 0x4000);
}

/**
 * @brief 获取消息类型名称（用于日志）
 */
inline const char* getMessageTypeName(uint16_t msg_id) {
    switch (msg_id) {
        case MSG_AGV_TELEMETRY:   return "AgvTelemetry";
        case MSG_MPC_TRAJECTORY:  return "MpcTrajectory";
        case MSG_TASK_FEEDBACK:   return "TaskFeedback";
        case MSG_AGV_COMMAND:     return "AgvCommand";
        case MSG_NAVIGATION_TASK: return "NavigationTask";
        case MSG_LATENCY_PROBE:   return "LatencyProbe";
        case MSG_COMMON_RESPONSE: return "CommonResponse";
        case MSG_HEARTBEAT:       return "Heartbeat";
        default:                  return "Unknown";
    }
}

} // namespace proto
} // namespace agv
