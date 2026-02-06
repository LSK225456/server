#ifndef LSK_MUDUO_BASE_COPYABLE_H
#define LSK_MUDUO_BASE_COPYABLE_H

namespace lsk_muduo {

/**
 * @brief 标记类可拷贝的 tag class
 * @note 继承此类表示该类可以安全地拷贝
 */
class copyable {
protected:
    copyable() = default;
    ~copyable() = default;
};

}  // namespace lsk_muduo

#endif  // LSK_MUDUO_BASE_COPYABLE_H
