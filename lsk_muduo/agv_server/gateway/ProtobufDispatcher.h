#ifndef LSK_MUDUO_GATEWAY_PROTOBUF_DISPATCHER_H
#define LSK_MUDUO_GATEWAY_PROTOBUF_DISPATCHER_H

#include "../../muduo/net/Callbacks.h"
#include "../../muduo/base/Logger.h"
#include <google/protobuf/message.h>
#include <map>
#include <memory>
#include <functional>
#include <type_traits>

namespace agv {
namespace gateway {

/**
 * @brief 模板化 Protobuf 消息分发器（迭代二：Day 1-2）
 * 
 * @note 核心设计思想：
 *       1. 类型安全：注册时绑定具体 Protobuf 类型，回调签名为 void(conn, const ConcreteMsg&)
 *       2. 反序列化 + 分发一体化：Dispatcher 内部完成 ParseFromArray -> 调用强类型回调
 *       3. 类型擦除：内部用基类指针 + 虚函数实现，外部只看到模板接口
 *       4. 默认回调：未注册的消息类型走 defaultCallback（可选）
 * 
 * @note 使用方式：
 *       ProtobufDispatcher dispatcher;
 *       dispatcher.registerHandler<AgvTelemetry>(MSG_AGV_TELEMETRY, 
 *           [this](const TcpConnectionPtr& conn, const AgvTelemetry& msg) {
 *               handleTelemetry(conn, msg);
 *           });
 *       // 在 onMessage 中调用：
 *       dispatcher.dispatch(conn, msg_type, payload, len);
 * 
 * @note 线程安全：
 *       - registerHandler() 应在启动前完成（非线程安全），运行时只读
 *       - dispatch() 是只读操作，多线程安全
 * 
 * @note 扩展性：
 *       - 新增消息类型只需一行 registerHandler 调用
 *       - 无需修改 Dispatcher 本身
 */
class ProtobufDispatcher {
public:
    /**
     * @brief 默认回调类型
     * @param conn 连接
     * @param msg_type 消息类型 ID
     * @param payload 原始字节流
     * @param len 字节流长度
     */
    using DefaultCallback = std::function<void(const lsk_muduo::TcpConnectionPtr& conn,
                                               uint16_t msg_type,
                                               const char* payload,
                                               size_t len)>;

    ProtobufDispatcher()
        : defaultCallback_(nullptr) {}

    explicit ProtobufDispatcher(DefaultCallback defaultCb)
        : defaultCallback_(std::move(defaultCb)) {}

    /**
     * @brief 注册消息处理函数（核心模板接口）
     * 
     * @tparam MessageT 具体的 Protobuf 消息类型（如 AgvTelemetry、Heartbeat）
     * @param msg_type 消息类型 ID（来自 message_id.h）
     * @param callback 强类型回调：void(const TcpConnectionPtr&, const MessageT&)
     * 
     * @note 编译期检查：MessageT 必须是 google::protobuf::Message 的子类
     * @note 运行时：dispatch() 将自动反序列化为 MessageT 并调用 callback
     * 
     * 使用示例：
     *   dispatcher.registerHandler<AgvTelemetry>(MSG_AGV_TELEMETRY,
     *       std::bind(&GatewayServer::handleTelemetry, this, _1, _2));
     */
    template <typename MessageT>
    void registerHandler(uint16_t msg_type,
                         std::function<void(const lsk_muduo::TcpConnectionPtr&,
                                            const MessageT&)> callback) {
        static_assert(std::is_base_of<google::protobuf::Message, MessageT>::value,
                      "MessageT must derive from google::protobuf::Message");
        
        handlers_[msg_type] = std::make_shared<TypedHandler<MessageT>>(std::move(callback));
    }

    /**
     * @brief 设置默认回调（处理未注册的消息类型）
     */
    void setDefaultCallback(DefaultCallback cb) {
        defaultCallback_ = std::move(cb);
    }

    /**
     * @brief 分发消息（替换原 handleProtobufMessage 中的 switch-case）
     * 
     * @param conn 连接对象
     * @param msg_type 消息类型 ID（从包头解析）
     * @param payload Protobuf 序列化字节流
     * @param len 字节流长度
     * @return true 分发成功（找到 handler 且反序列化成功）
     * @return false 分发失败（未注册 handler 或反序列化失败）
     * 
     * @note 流程：
     *       1. 根据 msg_type 查找注册的 handler
     *       2. handler 内部完成反序列化
     *       3. 反序列化成功后调用强类型回调
     *       4. 未找到 handler 则调用 defaultCallback（若有）
     */
    bool dispatch(const lsk_muduo::TcpConnectionPtr& conn,
                  uint16_t msg_type,
                  const char* payload,
                  size_t len) const {
        auto it = handlers_.find(msg_type);
        if (it != handlers_.end()) {
            return it->second->handle(conn, payload, len);
        }
        
        // 未注册的消息类型
        if (defaultCallback_) {
            defaultCallback_(conn, msg_type, payload, len);
            return true;  // 默认回调已处理
        }
        
        LOG_WARN << "ProtobufDispatcher: no handler for msg_type=0x" 
                 << std::hex << msg_type << std::dec;
        return false;
    }

    /**
     * @brief 查询是否已注册某消息类型
     */
    bool hasHandler(uint16_t msg_type) const {
        return handlers_.find(msg_type) != handlers_.end();
    }

    /**
     * @brief 获取已注册的 handler 数量
     */
    size_t handlerCount() const {
        return handlers_.size();
    }

private:
    // ==================== 类型擦除基类 ====================
    
    /**
     * @brief Handler 基类（类型擦除）
     * @note 将模板化的具体类型隐藏在虚函数后面
     *       使得 handlers_ 可以用统一的 map 存储不同类型的 handler
     */
    class HandlerBase {
    public:
        virtual ~HandlerBase() = default;
        
        /**
         * @brief 处理原始字节流：反序列化 + 调用回调
         * @return true 成功，false 反序列化失败
         */
        virtual bool handle(const lsk_muduo::TcpConnectionPtr& conn,
                           const char* payload,
                           size_t len) const = 0;
    };

    using HandlerPtr = std::shared_ptr<HandlerBase>;

    // ==================== 类型化的具体 Handler ====================
    
    /**
     * @brief 具体消息类型的 Handler（模板派生类）
     * 
     * @tparam MessageT 具体 Protobuf 消息类型
     * 
     * @note 每个 MessageT 对应一个 TypedHandler<MessageT> 实例
     *       handle() 内部：
     *       1. 创建 MessageT 实例
     *       2. ParseFromArray 反序列化
     *       3. 成功后调用 callback_(conn, msg)
     */
    template <typename MessageT>
    class TypedHandler : public HandlerBase {
    public:
        using Callback = std::function<void(const lsk_muduo::TcpConnectionPtr&,
                                            const MessageT&)>;

        explicit TypedHandler(Callback cb) : callback_(std::move(cb)) {}

        bool handle(const lsk_muduo::TcpConnectionPtr& conn,
                   const char* payload,
                   size_t len) const override {
            MessageT msg;
            if (!msg.ParseFromArray(payload, static_cast<int>(len))) {
                LOG_ERROR << "ProtobufDispatcher: failed to parse " 
                          << msg.GetTypeName();
                return false;
            }
            callback_(conn, msg);
            return true;
        }

    private:
        Callback callback_;
    };

    // ==================== 成员变量 ====================
    
    /// msg_type -> Handler 映射（运行时只读，无需加锁）
    std::map<uint16_t, HandlerPtr> handlers_;
    
    /// 默认回调（未注册的消息类型）
    DefaultCallback defaultCallback_;
};

}  // namespace gateway
}  // namespace agv

#endif  // LSK_MUDUO_GATEWAY_PROTOBUF_DISPATCHER_H
