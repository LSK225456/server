#ifndef LSK_MUDUO_GATEWAY_LATENCY_MONITOR_H
#define LSK_MUDUO_GATEWAY_LATENCY_MONITOR_H

#include "../../muduo/base/Timestamp.h"
#include "../../muduo/base/Logger.h"
#include "../proto/message.pb.h"
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <string>

namespace agv {
namespace gateway {

/**
 * @brief RTT 延迟监控器（迭代三：第6周 Day 3-4）
 * 
 * @note 核心功能：
 *       1. 创建 LatencyProbe Ping 消息（携带序列号和时间戳）
 *       2. 处理 LatencyProbe Pong 响应，计算 RTT（往返时间）
 *       3. 维护每个 AGV 的 RTT 统计数据（latest, avg, min, max）
 * 
 * @note 使用流程：
 *       - GatewayServer 每 N 秒调用 onLatencyTimer()
 *       - onLatencyTimer() 遍历所有 Session，调用 createPing() 创建探测消息并发送
 *       - 客户端收到 Ping 后回复 Pong（is_response=true, 保持原序列号和时间戳）
 *       - 服务器收到 Pong 后调用 processPong() 计算 RTT
 * 
 * @note 线程安全：
 *       - 所有操作通过 mutex 保护
 *       - createPing() 和 processPong() 可在不同线程调用
 */
class LatencyMonitor {
public:
    /**
     * @brief 单个 AGV 的 RTT 统计数据
     */
    struct RttStats {
        double latest_rtt_ms = 0.0;    ///< 最近一次 RTT（毫秒）
        double avg_rtt_ms = 0.0;       ///< 平均 RTT（毫秒）
        double min_rtt_ms = 1e9;       ///< 最小 RTT（毫秒）
        double max_rtt_ms = 0.0;       ///< 最大 RTT（毫秒）
        int64_t sample_count = 0;      ///< 采样次数
        double total_rtt_ms = 0.0;     ///< 累计 RTT（用于计算平均值）
    };

    LatencyMonitor();
    ~LatencyMonitor() = default;

    // 禁止拷贝
    LatencyMonitor(const LatencyMonitor&) = delete;
    LatencyMonitor& operator=(const LatencyMonitor&) = delete;

    /**
     * @brief 创建 Ping 探测消息
     * @param target_agv_id 目标车辆 ID
     * @return 构造好的 LatencyProbe 消息（is_response=false）
     * 
     * @note 内部记录 seq_num 和发送时间戳，用于后续 Pong 匹配
     */
    proto::LatencyProbe createPing(const std::string& target_agv_id);

    /**
     * @brief 处理 Pong 响应，计算 RTT
     * @param pong 客户端返回的 LatencyProbe 消息（is_response=true）
     * @return RTT（毫秒），如果 seq_num 未找到返回 -1.0
     * 
     * @note 内部自动更新对应 AGV 的统计数据
     */
    double processPong(const proto::LatencyProbe& pong);

    /**
     * @brief 获取指定 AGV 的 RTT 统计
     * @param agv_id 车辆 ID
     * @return RTT 统计数据（如果无数据，返回默认值）
     */
    RttStats getStats(const std::string& agv_id) const;

    /**
     * @brief 获取所有 AGV 的 RTT 统计快照
     */
    std::unordered_map<std::string, RttStats> getAllStats() const;

    /**
     * @brief 输出所有 RTT 统计到日志
     */
    void logAllStats() const;

    /**
     * @brief 获取待处理的探测数量（已发送 Ping 但未收到 Pong）
     */
    size_t pendingCount() const;

    /**
     * @brief 清理超时的 pending 探测条目
     * @param timeout_ms 超时阈值（毫秒），默认 30000ms（30秒）
     * @return 清理的条目数量
     * 
     * @note 应由 onLatencyTimer 定期调用，避免客户端不回复 Pong 导致内存泄漏
     */
    size_t cleanupExpiredProbes(double timeout_ms = 30000.0);

private:
    mutable std::mutex mutex_;
    std::atomic<uint64_t> next_seq_num_{1};

    /// 待匹配探测：seq_num -> (agv_id, send_timestamp_us)
    std::unordered_map<uint64_t, std::pair<std::string, int64_t>> pending_probes_;

    /// 每车 RTT 统计：agv_id -> RttStats
    std::unordered_map<std::string, RttStats> stats_;
};

}  // namespace gateway
}  // namespace agv

#endif  // LSK_MUDUO_GATEWAY_LATENCY_MONITOR_H
