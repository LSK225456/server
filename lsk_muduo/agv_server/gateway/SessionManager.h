#ifndef LSK_MUDUO_GATEWAY_SESSION_MANAGER_H
#define LSK_MUDUO_GATEWAY_SESSION_MANAGER_H

#include "AgvSession.h"
#include "ConcurrentMap.h"
#include "../../muduo/net/TcpConnection.h"
#include <string>
#include <memory>
#include <functional>
#include <vector>

namespace agv {
namespace gateway {

/**
 * @brief AGV 会话管理器（迭代二：Day 5-7）
 * 
 * @note 核心职责：
 *       1. 管理车辆会话的生命周期（注册、查找、移除）
 *       2. 封装 ConcurrentMap，提供类型安全的会话操作接口
 *       3. 支持批量操作（遍历、条件删除），用于看门狗等场景
 * 
 * @note 设计原则：
 *       - 单一职责：只负责会话 CRUD，不关心业务逻辑（低电量检查等）
 *       - 线程安全：内部使用 ConcurrentMap，所有操作都是线程安全的
 *       - 简洁封装：薄层封装，避免过度设计
 * 
 * @note 架构位置：
 *       GatewayServer (网络IO层)
 *           └── SessionManager (会话管理层)
 *                   └── ConcurrentMap<string, AgvSession>
 *                           └── AgvSession (持有 weak_ptr<TcpConnection>)
 * 
 * @note 与 GatewayServer 的职责划分：
 *       - GatewayServer：TCP 连接管理、消息编解码、业务逻辑
 *       - SessionManager：会话信息管理、会话生命周期
 */
class SessionManager {
public:
    SessionManager() = default;
    ~SessionManager() = default;

    // 禁止拷贝和移动
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    // ==================== 会话注册与查找 ====================

    /**
     * @brief 注册新会话
     * @param agv_id 车辆唯一标识符
     * @param conn TCP 连接对象
     * @return true 成功注册新会话，false 会话已存在（会被替换）
     * 
     * @note 调用场景：
     *       - 首次收到车辆消息时懒加载注册
     *       - 车辆重连时更新会话关联的连接
     * 
     * @note 线程安全：内部使用 ConcurrentMap::insert（线程安全）
     */
    bool registerSession(const std::string& agv_id,
                         const std::shared_ptr<lsk_muduo::TcpConnection>& conn);

    /**
     * @brief 查找会话
     * @param agv_id 车辆 ID
     * @return shared_ptr<AgvSession> 找到返回会话对象，未找到返回 nullptr
     * 
     * @note 线程安全：
     *       - 返回 shared_ptr 拷贝，即使其他线程删除该会话，已返回的指针仍有效
     *       - ConcurrentMap::find 使用读锁，多个线程可并发查找
     */
    AgvSessionPtr findSession(const std::string& agv_id) const;

    /**
     * @brief 检查会话是否存在
     * @param agv_id 车辆 ID
     * @return true 存在，false 不存在
     */
    bool hasSession(const std::string& agv_id) const;

    // ==================== 会话移除 ====================

    /**
     * @brief 移除指定会话
     * @param agv_id 车辆 ID
     * @return true 成功移除，false 会话不存在
     * 
     * @note 调用场景：
     *       - 连接断开时清理会话
     *       - 看门狗检测到超时，手动清理
     */
    bool removeSession(const std::string& agv_id);

    /**
     * @brief 根据连接对象反查并移除会话
     * @param conn 连接对象
     * @return 被移除的会话数量（通常为 0 或 1）
     * 
     * @note 调用场景：
     *       - onConnection(conn, false) 连接断开回调中
     *       - 尚未收到 agv_id 时，无法通过 ID 查找，只能通过连接对象反查
     * 
     * @note 实现方式：
     *       - 遍历所有会话，比较 session->getConnection().get() == conn.get()
     *       - 使用写锁保护，批量删除
     */
    size_t removeSessionByConnection(const std::shared_ptr<lsk_muduo::TcpConnection>& conn);

    /**
     * @brief 清空所有会话
     * @note 调用场景：服务器关闭时清理资源
     */
    void clear();

    // ==================== 批量操作 ====================

    /**
     * @brief 遍历所有会话
     * @param func 回调函数：void(const string& agv_id, const AgvSessionPtr& session)
     * 
     * @note 调用场景：
     *       - 看门狗定时器遍历检查超时
     *       - 统计模块收集所有车辆状态
     * 
     * @note 注意事项：
     *       - 回调中持有读锁，不应执行耗时操作
     *       - 回调中不应调用 removeSession（会死锁）
     *       - 如需删除，使用 eraseIf
     */
    void forEach(std::function<void(const std::string&, const AgvSessionPtr&)> func) const;

    /**
     * @brief 条件删除会话
     * @param predicate 谓词：返回 true 表示删除该会话
     * @return 被删除的会话数量
     * 
     * @note 调用场景：
     *       - 看门狗批量清理超时会话
     *       - 批量清理特定状态的会话（如所有 OFFLINE 会话）
     * 
     * @note 示例：
     *       // 删除所有超时的会话
     *       manager.eraseIf([now](const string& id, const AgvSessionPtr& s) {
     *           return timeDiff(now, s->getLastActiveTime()) > 5000;
     *       });
     */
    size_t eraseIf(std::function<bool(const std::string&, const AgvSessionPtr&)> predicate);

    // ==================== 统计查询 ====================

    /**
     * @brief 获取当前会话数量
     */
    size_t size() const;

    /**
     * @brief 是否为空
     */
    bool empty() const;

    /**
     * @brief 获取所有车辆 ID 列表
     * @return 车辆 ID 列表（快照）
     * 
     * @note 返回拷贝，调用者可安全遍历（不持锁）
     */
    std::vector<std::string> getAllAgvIds() const;

private:
    /// 会话容器：agv_id -> AgvSession（线程安全）
    ConcurrentMap<std::string, AgvSession> sessions_;
};

}  // namespace gateway
}  // namespace agv

#endif  // LSK_MUDUO_GATEWAY_SESSION_MANAGER_H
