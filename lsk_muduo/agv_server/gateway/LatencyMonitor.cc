#include "LatencyMonitor.h"

namespace agv {
namespace gateway {

LatencyMonitor::LatencyMonitor() = default;

proto::LatencyProbe LatencyMonitor::createPing(const std::string& target_agv_id) {
    proto::LatencyProbe probe;
    probe.set_target_agv_id(target_agv_id);

    int64_t now_us = lsk_muduo::Timestamp::now().microSecondsSinceEpoch();
    probe.set_send_timestamp(now_us);

    uint64_t seq = next_seq_num_.fetch_add(1, std::memory_order_relaxed);
    probe.set_seq_num(seq);
    probe.set_is_response(false);

    // 记录待匹配的探测
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_probes_[seq] = {target_agv_id, now_us};
    }

    LOG_DEBUG << "[LatencyMonitor] Created Ping for [" << target_agv_id
              << "] seq=" << seq;

    return probe;
}

double LatencyMonitor::processPong(const proto::LatencyProbe& pong) {
    if (!pong.is_response()) {
        LOG_WARN << "[LatencyMonitor] Received non-response probe, ignoring";
        return -1.0;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = pending_probes_.find(pong.seq_num());
    if (it == pending_probes_.end()) {
        LOG_WARN << "[LatencyMonitor] Unknown seq_num=" << pong.seq_num()
                 << ", ignoring";
        return -1.0;
    }

    const std::string& agv_id = it->second.first;
    int64_t send_time_us = it->second.second;
    pending_probes_.erase(it);

    // 计算 RTT
    int64_t now_us = lsk_muduo::Timestamp::now().microSecondsSinceEpoch();
    double rtt_ms = static_cast<double>(now_us - send_time_us) / 1000.0;

    // 更新统计
    RttStats& stats = stats_[agv_id];
    stats.latest_rtt_ms = rtt_ms;
    stats.sample_count++;
    stats.total_rtt_ms += rtt_ms;
    stats.avg_rtt_ms = stats.total_rtt_ms / static_cast<double>(stats.sample_count);
    if (rtt_ms < stats.min_rtt_ms) stats.min_rtt_ms = rtt_ms;
    if (rtt_ms > stats.max_rtt_ms) stats.max_rtt_ms = rtt_ms;

    LOG_DEBUG << "[LatencyMonitor] Pong from [" << agv_id
              << "] RTT=" << rtt_ms << "ms"
              << " (avg=" << stats.avg_rtt_ms
              << "ms, count=" << stats.sample_count << ")";

    return rtt_ms;
}

LatencyMonitor::RttStats LatencyMonitor::getStats(const std::string& agv_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = stats_.find(agv_id);
    if (it != stats_.end()) {
        return it->second;
    }
    return RttStats{};
}

std::unordered_map<std::string, LatencyMonitor::RttStats>
LatencyMonitor::getAllStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void LatencyMonitor::logAllStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stats_.empty()) {
        LOG_INFO << "[LatencyMonitor] No RTT data available";
        return;
    }

    LOG_INFO << "[LatencyMonitor] ========== RTT Statistics ==========";
    for (const auto& pair : stats_) {
        const std::string& agv_id = pair.first;
        const RttStats& s = pair.second;
        LOG_INFO << "  [" << agv_id << "] "
                 << "latest=" << s.latest_rtt_ms << "ms, "
                 << "avg=" << s.avg_rtt_ms << "ms, "
                 << "min=" << s.min_rtt_ms << "ms, "
                 << "max=" << s.max_rtt_ms << "ms, "
                 << "count=" << s.sample_count;
    }
    LOG_INFO << "[LatencyMonitor] ====================================";
}

size_t LatencyMonitor::pendingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_probes_.size();
}

size_t LatencyMonitor::cleanupExpiredProbes(double timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    int64_t now_us = lsk_muduo::Timestamp::now().microSecondsSinceEpoch();
    int64_t timeout_us = static_cast<int64_t>(timeout_ms * 1000.0);
    size_t removed = 0;

    for (auto it = pending_probes_.begin(); it != pending_probes_.end(); ) {
        int64_t elapsed_us = now_us - it->second.second;
        if (elapsed_us > timeout_us) {
            LOG_DEBUG << "[LatencyMonitor] Expired probe for [" << it->second.first
                      << "] seq=" << it->first << " (elapsed=" << elapsed_us / 1000.0 << "ms)";
            it = pending_probes_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }

    if (removed > 0) {
        LOG_INFO << "[LatencyMonitor] Cleaned up " << removed << " expired pending probes";
    }
    return removed;
}

}  // namespace gateway
}  // namespace agv
