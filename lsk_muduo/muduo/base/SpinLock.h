#ifndef LSK_MUDUO_BASE_SPINLOCK_H
#define LSK_MUDUO_BASE_SPINLOCK_H

#include <atomic>

namespace lsk_muduo {

/**
 * @brief 自旋锁（TTAS算法 - Test-And-Test-And-Set）
 * 
 * @note 设计要点：
 *       - 适用场景：锁持有时间极短（<10μs），竞争不激烈
 *       - TTAS优化：先用load测试，减少CAS操作的cache颠簸
 *       - 性能：无竞争时~10ns，有竞争时自旋等待
 *       - 对比mutex：mutex约100ns开销（因为涉及系统调用）
 * 
 * @note 使用场景（AgvSession位姿更新）：
 *       - IO线程50Hz更新位姿（每次更新<1μs）
 *       - Worker线程偶尔读取位姿（<1μs）
 *       - 竞争概率极低（50Hz = 20ms间隔 >> 1μs锁持有时间）
 * 
 * @warning 不适用场景：
 *       - 锁持有时间长（会导致CPU空转浪费）
 *       - 高竞争场景（自旋会导致性能下降）
 *       - 锁内有阻塞操作（IO、系统调用等）
 */
class SpinLock {
public:
    /**
     * @brief 默认构造函数
     */
    SpinLock() : flag_(false) {}
    
    /// 禁止拷贝
    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;

    /**
     * @brief 加锁（阻塞直到获取锁）
     * 
     * @note TTAS算法：
     *       1. 先用load测试锁是否空闲（不修改cache line）
     *       2. 空闲时才用exchange尝试获取（CAS操作）
     *       3. 失败则继续自旋
     */
    void lock() {
        // TTAS优化：先读后写，减少cache颠簸
        while (true) {
            // 1. Test: 用load测试锁状态（不修改cache line，减少总线流量）
            while (flag_.load(std::memory_order_relaxed)) {
                // 自旋等待：可选添加CPU pause指令减少功耗
                // __builtin_ia32_pause();  // x86架构
            }
            
            // 2. Test-And-Set: 锁空闲后尝试获取
            if (!flag_.exchange(true, std::memory_order_acquire)) {
                // 成功获取锁
                return;
            }
            // 失败则回到步骤1继续等待
        }
    }

    /**
     * @brief 解锁
     * @note 使用memory_order_release确保临界区写操作对其他线程可见
     */
    void unlock() {
        flag_.store(false, std::memory_order_release);
    }

    /**
     * @brief 尝试加锁（非阻塞）
     * @return true 获取锁成功，false 锁被占用
     */
    bool tryLock() {
        // 直接尝试exchange，不自旋
        return !flag_.exchange(true, std::memory_order_acquire);
    }

private:
    std::atomic<bool> flag_;  ///< 锁标志：false=空闲，true=占用
};

/**
 * @brief 自旋锁守卫（RAII）
 * 
 * @note 用法：
 *   SpinLock lock;
 *   {
 *       SpinLockGuard guard(lock);
 *       // 临界区代码
 *   }  // 自动解锁
 */
class SpinLockGuard {
public:
    /**
     * @brief 构造时加锁
     */
    explicit SpinLockGuard(SpinLock& lock) : lock_(lock) {
        lock_.lock();
    }

    /**
     * @brief 析构时解锁
     */
    ~SpinLockGuard() {
        lock_.unlock();
    }

    /// 禁止拷贝和移动
    SpinLockGuard(const SpinLockGuard&) = delete;
    SpinLockGuard& operator=(const SpinLockGuard&) = delete;

private:
    SpinLock& lock_;
};

}  // namespace lsk_muduo

#endif  // LSK_MUDUO_BASE_SPINLOCK_H
