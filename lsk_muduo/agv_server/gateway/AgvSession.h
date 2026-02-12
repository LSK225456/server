#ifndef LSK_MUDUO_GATEWAY_AGV_SESSION_H
#define LSK_MUDUO_GATEWAY_AGV_SESSION_H

#include "../../muduo/base/Timestamp.h"
#include "../../muduo/net/TcpConnection.h"
#include "../../muduo/base/SpinLock.h"
#include <string>
#include <memory>
#include <mutex>

namespace agv {
namespace gateway {

/**
 * @brief AGV 车辆会话信息
 * 
 * @note 设计要点：
 *       - 线程安全：使用 MutexLock 保护所有可变字段
 *       - 生命周期：由 GatewayServer 管理，shared_ptr 引用计数自动释放
 *       - 扩展性：预留位姿、轨迹等字段，迭代二补充
 * 
 * @note 并发场景：
 *       - IO 线程：在 onMessage 中更新 last_active_time、battery_level
 *       - Timer 线程：在看门狗中读取 last_active_time、state
 *       - 需要保证读写原子性，避免数据竞争
 */
class AgvSession {
public:
    /**
     * @brief 车辆状态枚举
     */
    enum State {
        ONLINE,      ///< 在线：正常收到遥测数据
        OFFLINE,     ///< 离线：超过 1 秒未收到任何消息（看门狗检测）
        CHARGING     ///< 充电中：已下发充电指令，避免重复触发
    };

    /**
     * @brief 位姿信息（预留扩展）
     * @note 迭代二会存储完整的 Telemetry 消息
     */
    struct Pose {
        double x;
        double y;
        double theta;
        double confidence;
    };

    // ==================== 构造与析构 ====================
    
    /**
     * @brief 构造函数
     * @param id 车辆唯一标识符
     * @param conn 连接对象（存储为 weak_ptr）
     * 
     * @note 存储弱引用而非强引用的原因：
     *       - 避免循环引用：TcpConnection 可能在其 context 中持有 Session
     *       - 不阻止连接关闭：连接断开时，TcpConnection 应能正常析构
     *       - 使用时通过 lock() 检查连接是否仍有效
     */
    AgvSession(const std::string& id,
               const std::shared_ptr<lsk_muduo::TcpConnection>& conn);
    ~AgvSession() = default;

    // 禁止拷贝，允许移动
    AgvSession(const AgvSession&) = delete;
    AgvSession& operator=(const AgvSession&) = delete;

    // ==================== 线程安全的 Getters ====================
    
    /**
     * @brief 获取车辆 ID（不可变，无需加锁）
     */
    std::string getAgvId() const { return agv_id_; }
    
    /**
     * @brief 获取最后活跃时间
     * @note Timer 线程调用，用于看门狗检测
     */
    lsk_muduo::Timestamp getLastActiveTime() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_active_time_;
    }

    /**
     * @brief 获取电池电量
     * @note IO 线程或业务引擎调用
     */
    double getBatteryLevel() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return battery_level_;
    }

    /**
     * @brief 获取车辆状态
     * @note 多线程读取，需加锁
     */
    State getState() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }

    /**
     * @brief 获取位姿信息
     * @note 使用SpinLock保护（迭代三：Day 5-7 快慢分离优化）
     * @note 高频访问场景（IO线程50Hz更新，Worker线程偶尔读取）
     */
    Pose getPose() const {
        lsk_muduo::SpinLockGuard lock(pose_mutex_);
        return pose_;
    }

    /**
     * @brief 获取连接对象（提升为 shared_ptr）
     * @return shared_ptr 如果连接仍然有效，否则返回空指针
     * 
     * @note 使用方式：
     *       auto conn = session->getConnection();
     *       if (conn) {
     *           // 连接有效，可以安全使用
     *           conn->send(data);
     *       }
     */
    std::shared_ptr<lsk_muduo::TcpConnection> getConnection() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return conn_.lock();  // 将 weak_ptr 提升为 shared_ptr
        // 尝试升级：如果连接还活着，返回 shared_ptr；否则返回 nullptr
    }

    // ==================== 线程安全的 Setters ====================
    
    /**
     * @brief 刷新最后活跃时间
     * @note 收到任何消息时调用（Telemetry、Heartbeat）
     * @note 调用场景：IO 线程的 onMessage 回调中
     */
    void updateActiveTime() {
        std::lock_guard<std::mutex> lock(mutex_);
        last_active_time_ = lsk_muduo::Timestamp::now();
    }

    /**
     * @brief 更新电池电量
     * @param level 电量百分比 [0.0, 100.0]
     * @note IO 线程调用，从 Telemetry 消息中提取
     */
    void updateBatteryLevel(double level) {
        std::lock_guard<std::mutex> lock(mutex_);
        battery_level_ = level;
    }

    /**
     * @brief 更新位姿信息
     * @param x X 坐标（米）
     * @param y Y 坐标（米）
     * @param theta 航向角（度）
     * @param confidence 定位置信度 [0.0, 1.0]
     * @note IO 线程调用（50Hz高频）
     * @note 使用SpinLock保护（迭代三：Day 5-7 快慢分离优化）
     */
    void updatePose(double x, double y, double theta, double confidence) {
        lsk_muduo::SpinLockGuard lock(pose_mutex_);
        pose_.x = x;
        pose_.y = y;
        pose_.theta = theta;
        pose_.confidence = confidence;
    }

    /**
     * @brief 设置车辆状态
     * @note 调用场景：
     *       - Timer 线程：看门狗检测超时，设置 OFFLINE
     *       - IO 线程：业务引擎下发充电指令，设置 CHARGING
     */
    void setState(State state) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = state;
    }

    /**
     * @brief 更新连接对象
     * @param conn 新的连接对象
     * 
     * @note 使用场景：
     *       - 车辆重连时，更新会话关联的连接
     *       - 典型场景：车辆离线后重新上线，复用原有会话
     */
    void setConnection(const std::shared_ptr<lsk_muduo::TcpConnection>& conn) {
        std::lock_guard<std::mutex> lock(mutex_);
        conn_ = conn;  // 存储弱引用
    }

private:
    // ==================== 不可变字段（无需加锁）====================
    
    const std::string agv_id_;  ///< 车辆唯一标识符

    // ==================== 可变字段（需加锁保护）====================
    
    mutable std::mutex mutex_;                              ///< 互斥锁，保护通用字段
    std::weak_ptr<lsk_muduo::TcpConnection> conn_;          ///< 连接弱引用（符合文档要求）
    lsk_muduo::Timestamp  last_active_time_;                ///< 最后活跃时间（看门狗用）
    double battery_level_;                                  ///< 电池电量 [0.0, 100.0]
    State state_;                                           ///< 当前状态
    
    /// 位姿字段独立保护（迭代三：Day 5-7 快慢分离优化）
    mutable lsk_muduo::SpinLock pose_mutex_;                ///< 自旋锁，保护位姿字段
    Pose pose_;                                             ///< 位姿信息（高频访问）
};

/// 会话智能指针类型
using AgvSessionPtr = std::shared_ptr<AgvSession>;

}  // namespace gateway
}  // namespace agv

#endif  // LSK_MUDUO_GATEWAY_AGV_SESSION_H
