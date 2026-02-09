#include "SessionManager.h"
#include "../../muduo/base/Logger.h"

namespace agv {
namespace gateway {

// ==================== 会话注册与查找 ====================

bool SessionManager::registerSession(const std::string& agv_id,
                                     const std::shared_ptr<lsk_muduo::TcpConnection>& conn) {
    // 检查会话是否已存在
    auto existingSession = sessions_.find(agv_id);
    if (existingSession) {
        LOG_WARN << "Session [" << agv_id << "] already exists, updating connection";
        // 更新已有会话的连接
        existingSession->setConnection(conn);
        return false;  // 表示不是首次注册
    }
    
    // 创建新会话（AgvSession 构造函数需要 conn）
    auto newSession = std::make_shared<AgvSession>(agv_id, conn);
    sessions_.insert(agv_id, newSession);
    
    LOG_INFO << "Session registered: [" << agv_id << "] from "
             << conn->peerAddress().toIpPort();
    
    return true;  // 表示成功注册新会话
}

AgvSessionPtr SessionManager::findSession(const std::string& agv_id) const {
    return sessions_.find(agv_id);
}

bool SessionManager::hasSession(const std::string& agv_id) const {
    return sessions_.contains(agv_id);
}

// ==================== 会话移除 ====================

bool SessionManager::removeSession(const std::string& agv_id) {
    bool removed = sessions_.erase(agv_id);
    if (removed) {
        LOG_INFO << "Session removed: [" << agv_id << "]";
    }
    return removed;
}

size_t SessionManager::removeSessionByConnection(
    const std::shared_ptr<lsk_muduo::TcpConnection>& conn) {
    
    // 使用 eraseIf 原子地查找并删除匹配的会话
    size_t count = sessions_.eraseIf(
        [&conn](const std::string& agv_id, const AgvSessionPtr& session) {
            // 获取会话关联的连接
            auto sessionConn = session->getConnection();
            if (!sessionConn) {
                // 连接已失效，但这里只删除匹配的连接
                return false;
            }
            
            // 比较连接指针（原始指针地址）
            bool match = (sessionConn.get() == conn.get());
            if (match) {
                LOG_WARN << "AGV [" << agv_id << "] connection lost, removing session";
            }
            return match;
        }
    );
    
    return count;
}

void SessionManager::clear() {
    size_t oldSize = sessions_.size();
    sessions_.clear();
    LOG_INFO << "All sessions cleared, count: " << oldSize;
}

// ==================== 批量操作 ====================

void SessionManager::forEach(
    std::function<void(const std::string&, const AgvSessionPtr&)> func) const {
    sessions_.forEach(func);
}

size_t SessionManager::eraseIf(
    std::function<bool(const std::string&, const AgvSessionPtr&)> predicate) {
    return sessions_.eraseIf(predicate);
}

// ==================== 统计查询 ====================

size_t SessionManager::size() const {
    return sessions_.size();
}

bool SessionManager::empty() const {
    return sessions_.empty();
}

std::vector<std::string> SessionManager::getAllAgvIds() const {
    return sessions_.keys();
}

}  // namespace gateway
}  // namespace agv
