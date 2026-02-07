#ifndef LSK_MUDUO_GATEWAY_CONCURRENT_MAP_H
#define LSK_MUDUO_GATEWAY_CONCURRENT_MAP_H

#include <unordered_map>
#include <shared_mutex>
#include <memory>
#include <functional>
#include <vector>

namespace agv {
namespace gateway {

/**
 * @brief 线程安全的哈希映射容器（迭代二：Day 3-4）
 * 
 * @tparam Key   键类型
 * @tparam Value 值类型（存储为 shared_ptr<Value>）
 * 
 * @note 核心设计思想：
 *       1. 读写锁（shared_mutex）：读多写少场景下优于互斥锁
 *          - 多个读者可同时持有 shared_lock（读操作并发）
 *          - 写者持有 unique_lock（独占访问）
 *       2. find 返回 shared_ptr 拷贝：调用者持有的 shared_ptr 保证
 *          对象生命周期安全，即使另一线程 erase 了该键，已返回的
 *          shared_ptr 仍有效（引用计数未归零）
 *       3. header-only 实现：模板类无需 .cc 文件
 * 
 * @note 应用场景：
 *       - GatewayServer 中的 sessions_ 和 connections_ 替换 std::map
 *       - 支持 IO 线程和 Timer 线程并发读写
 * 
 * @note 线程安全保证：
 *       - insert / erase / clear -> unique_lock（写锁，独占）
 *       - find / contains / size / forEach -> shared_lock（读锁，并发）
 *       - find 返回 shared_ptr 拷贝，拷贝完成后释放锁，不会长期持锁
 * 
 * @note 性能特征：
 *       - unordered_map 平均 O(1) 查找/插入/删除
 *       - shared_mutex 在读多写少场景下优于 mutex
 *       - find 返回 shared_ptr 拷贝有一次原子引用计数递增开销
 * 
 * @note 与 std::map 的区别：
 *       - std::map 有序（红黑树），unordered_map 无序（哈希表）
 *       - 会话管理场景不需要有序遍历，哈希表更快
 */
template <typename Key, typename Value>
class ConcurrentMap {
public:
    using ValuePtr = std::shared_ptr<Value>;

    ConcurrentMap() = default;
    ~ConcurrentMap() = default;

    // 禁止拷贝（含锁，不可拷贝）
    ConcurrentMap(const ConcurrentMap&) = delete;
    ConcurrentMap& operator=(const ConcurrentMap&) = delete;

    // ==================== 写操作（unique_lock）====================

    /**
     * @brief 插入或更新键值对
     * @param key 键
     * @param value 值（shared_ptr）
     * @return true 新插入，false 更新已有键
     * 
     * @note 如果 key 已存在，替换其 value（类似 map[key] = value）
     */
    bool insert(const Key& key, ValuePtr value) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto [it, inserted] = map_.insert_or_assign(key, std::move(value));
        return inserted;
    }

    /**
     * @brief 插入（仅当 key 不存在时）
     * @return true 成功插入，false key 已存在（未修改）
     */
    bool insertIfAbsent(const Key& key, ValuePtr value) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto result = map_.insert(std::make_pair(key, std::move(value)));
        return result.second;
    }

    /**
     * @brief 删除指定键
     * @return true 找到并删除，false 键不存在
     */
    bool erase(const Key& key) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        return map_.erase(key) > 0;
    }

    /**
     * @brief 清空所有元素
     */
    void clear() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        map_.clear();
    }

    // ==================== 读操作（shared_lock）====================

    /**
     * @brief 查找键对应的值
     * @return shared_ptr 拷贝（未找到返回空 shared_ptr）
     * 
     * @note 关键设计：返回 shared_ptr 的拷贝而非引用/原始指针
     *       - 拷贝后释放读锁，不会长期持锁
     *       - 即使其他线程 erase 了该 key，已持有的 shared_ptr 仍有效
     *       - 引用计数保证对象不会被提前析构
     */
    ValuePtr find(const Key& key) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            return it->second;  // 返回 shared_ptr 拷贝
        }
        return nullptr;
    }

    /**
     * @brief 检查键是否存在
     */
    bool contains(const Key& key) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return map_.find(key) != map_.end();
    }

    /**
     * @brief 获取元素数量
     */
    size_t size() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return map_.size();
    }

    /**
     * @brief 是否为空
     */
    bool empty() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return map_.empty();
    }

    /**
     * @brief 安全遍历所有元素（读锁保护下）
     * @param func 遍历回调：void(const Key&, const ValuePtr&)
     * 
     * @note 回调中不应执行耗时操作（持锁期间会阻塞写操作）
     * @note 回调中不应调用本 ConcurrentMap 的写方法（会死锁）
     * @note 适用场景：看门狗遍历检查、状态统计
     */
    void forEach(std::function<void(const Key&, const ValuePtr&)> func) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        for (const auto& pair : map_) {
            func(pair.first, pair.second);
        }
    }

    /**
     * @brief 在写锁保护下遍历并可删除元素
     * @param predicate 谓词：返回 true 表示删除该元素
     * @return 被删除的元素数量
     * 
     * @note 写锁保护，遍历期间独占访问
     * @note 适用场景：看门狗批量清理超时会话
     */
    size_t eraseIf(std::function<bool(const Key&, const ValuePtr&)> predicate) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        size_t count = 0;
        for (auto it = map_.begin(); it != map_.end(); ) {
            if (predicate(it->first, it->second)) {
                it = map_.erase(it);
                ++count;
            } else {
                ++it;
            }
        }
        return count;
    }

    /**
     * @brief 获取所有键的快照
     * @return 键列表的拷贝
     * 
     * @note 返回拷贝后释放锁，调用者可安全遍历
     */
    std::vector<Key> keys() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        std::vector<Key> result;
        result.reserve(map_.size());
        for (const auto& pair : map_) {
            result.push_back(pair.first);
        }
        return result;
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<Key, ValuePtr> map_;
};

}  // namespace gateway
}  // namespace agv

#endif  // LSK_MUDUO_GATEWAY_CONCURRENT_MAP_H
