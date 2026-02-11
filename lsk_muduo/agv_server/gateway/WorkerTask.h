#ifndef LSK_MUDUO_GATEWAY_WORKER_TASK_H
#define LSK_MUDUO_GATEWAY_WORKER_TASK_H

#include "../../muduo/net/TcpConnection.h"
#include "../../muduo/base/Timestamp.h"
#include "AgvSession.h"
#include <google/protobuf/message.h>
#include <memory>
#include <string>

namespace agv {
namespace gateway {

/**
 * @brief Worker线程任务封装（迭代三：Day 3-4）
 * 
 * @note 设计要点：
 *       1. 弱引用连接（weak_ptr）：避免循环引用，连接断开时能正确检测
 *       2. 强引用会话（shared_ptr）：确保任务执行期间会话对象有效
 *       3. Protobuf基类指针：支持类型安全的消息传递，无需重新序列化
 *       4. 时间戳：用于统计任务排队延迟和处理延迟
 * 
 * @note 使用场景：
 *       - 耗时的数据库操作（存储遥测数据、任务日志等）
 *       - 复杂的业务逻辑（路径规划、任务调度决策等）
 *       - 避免阻塞IO线程，保证高频消息（Telemetry 50Hz）不受影响
 * 
 * @note 线程安全：
 *       - IO线程：构造任务并投递到ThreadPool
 *       - Worker线程：执行processTask
 *       - IO线程：通过runInLoop回调发送响应
 */
struct WorkerTask {
    // ==================== 核心字段 ====================
    
    /**
     * @brief 连接对象弱引用
     * @note 使用weak_ptr的原因：
     *       - 连接可能在任务排队期间断开
     *       - Worker线程处理前需要lock()检查连接有效性
     *       - 避免延长连接生命周期（不阻止TcpConnection析构）
     */
    lsk_muduo::TcpConnectionWeakPtr conn;
    
    /**
     * @brief 会话对象强引用
     * @note 使用shared_ptr的原因：
     *       - 确保任务执行期间会话数据有效
     *       - 支持并发访问（会话内部有mutex保护）
     */
    AgvSessionPtr session;
    
    /**
     * @brief Protobuf消息基类指针
     * @note 使用shared_ptr<Message>的原因：
     *       - 避免重新序列化（性能优化）
     *       - 支持多态（Worker可统一处理不同消息类型）
     *       - 通过getMessage<T>()进行类型安全的向下转型
     */
    std::shared_ptr<google::protobuf::Message> message;
    
    /**
     * @brief 消息类型ID（用于日志和统计）
     */
    int32_t msg_type;
    
    /**
     * @brief 任务提交时间戳
     * @note 用途：
     *       - 计算排队延迟：processTime - submit_time
     *       - 统计P99/P999延迟分布
     *       - 检测任务积压（队列过长时告警）
     */
    lsk_muduo::Timestamp submit_time;
    
    // ==================== 构造函数 ====================
    
    /**
     * @brief 默认构造函数
     */
    WorkerTask()
        : msg_type(0),
          submit_time(lsk_muduo::Timestamp::now()) {}
    
    /**
     * @brief 完整构造函数
     * @param conn_ptr 连接对象（将存储为weak_ptr）
     * @param sess_ptr 会话对象（强引用）
     * @param msg_ptr Protobuf消息指针
     * @param type 消息类型ID
     */
    WorkerTask(const lsk_muduo::TcpConnectionPtr& conn_ptr,
               const AgvSessionPtr& sess_ptr,
               const std::shared_ptr<google::protobuf::Message>& msg_ptr,
               int32_t type)
        : conn(conn_ptr),  // TcpConnectionPtr -> TcpConnectionWeakPtr 自动转换
          session(sess_ptr),
          message(msg_ptr),
          msg_type(type),
          submit_time(lsk_muduo::Timestamp::now()) {}
    
    // ==================== 辅助方法 ====================
    
    /**
     * @brief 类型安全的消息提取
     * @tparam T 具体的Protobuf消息类型（如NavigationTask）
     * @return shared_ptr<T> 如果类型匹配返回指针，否则返回nullptr
     * 
     * @example
     *   auto nav_task = task.getMessage<proto::NavigationTask>();
     *   if (nav_task) {
     *       // 处理NavigationTask
     *   }
     */
    template<typename T>
    std::shared_ptr<T> getMessage() const {
        return std::dynamic_pointer_cast<T>(message);
    }
    
    /**
     * @brief 检查连接是否仍然有效
     * @return TcpConnectionPtr 如果连接有效返回shared_ptr，否则返回nullptr
     * 
     * @note 使用方式：
     *   auto conn_locked = task.getConnection();
     *   if (conn_locked) {
     *       // 连接有效，可发送响应
     *   } else {
     *       // 连接已断开，任务取消
     *   }
     */
    lsk_muduo::TcpConnectionPtr getConnection() const {
        return conn.lock();
    }
    
    /**
     * @brief 计算任务排队时长（毫秒）
     */
    double getQueueLatencyMs() const {
        lsk_muduo::Timestamp now = lsk_muduo::Timestamp::now();
        int64_t diff = now.microSecondsSinceEpoch() - submit_time.microSecondsSinceEpoch();
        return static_cast<double>(diff) / 1000.0;  // 微秒 -> 毫秒
    }
};

}  // namespace gateway
}  // namespace agv

#endif  // LSK_MUDUO_GATEWAY_WORKER_TASK_H
