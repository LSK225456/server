#pragma once

#include <functional>
#include <memory>

/**
 * WeakCallback - 弱回调包装器
 * 
 * 使用 weak_ptr 防止异步回调时对象已析构导致的野指针问题。
 * 典型场景：forceCloseWithDelay() 定时器到期时，TcpConnection 可能已被销毁。
 * 
 * 工作原理：
 * 1. 持有目标对象的 weak_ptr（不增加引用计数）
 * 2. 回调触发时，尝试 lock() 提升为 shared_ptr
 * 3. 如果对象仍存活，调用成员函数；否则静默忽略
 * 
 * 面试考点：
 * - weak_ptr 使用场景：打破循环引用、防止悬空回调
 * - shared_ptr::lock() 的线程安全性
 * - 完美转发（std::forward）在模板中的应用
 */

template<typename CLASS, typename... ARGS>
class WeakCallback
{
public:
    WeakCallback(const std::weak_ptr<CLASS>& object,
                 const std::function<void (CLASS*, ARGS...)>& function)
        : object_(object), function_(function)
    {
    }

    // 默认析构、拷贝构造和赋值操作符都可以使用

    // 函数调用运算符 - 当回调被触发时调用
    void operator()(ARGS&&... args) const
    {
        // 尝试提升 weak_ptr 为 shared_ptr
        std::shared_ptr<CLASS> ptr(object_.lock());
        if (ptr)
        {
            // 对象仍然存活，安全调用成员函数
            function_(ptr.get(), std::forward<ARGS>(args)...);
        }
        // 对象已析构，静默忽略（这正是我们想要的行为）
    }

private:
    std::weak_ptr<CLASS> object_;                       // 弱引用目标对象
    std::function<void (CLASS*, ARGS...)> function_;    // 成员函数包装
};

/**
 * makeWeakCallback - 工厂函数（非 const 成员函数版本）
 * 
 * 使用示例：
 *   loop_->runAfter(seconds, makeWeakCallback(shared_from_this(), &TcpConnection::forceClose));
 * 
 * @param object   目标对象的 shared_ptr
 * @param function 成员函数指针
 * @return         WeakCallback 包装器
 */
template<typename CLASS, typename... ARGS>
WeakCallback<CLASS, ARGS...> makeWeakCallback(const std::shared_ptr<CLASS>& object,
                                              void (CLASS::*function)(ARGS...))
{
    return WeakCallback<CLASS, ARGS...>(object, function);
}

/**
 * makeWeakCallback - 工厂函数（const 成员函数版本）
 */
template<typename CLASS, typename... ARGS>
WeakCallback<CLASS, ARGS...> makeWeakCallback(const std::shared_ptr<CLASS>& object,
                                              void (CLASS::*function)(ARGS...) const)
{
    return WeakCallback<CLASS, ARGS...>(object, function);
}
