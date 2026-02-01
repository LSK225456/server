#pragma once

// TCP 连接在逻辑上是独占的，不能被拷贝，只能被引用（通过指针或引用）或移动
class noncopyable
{
public:
    // 派生类对象无法进行拷贝构造和赋值TcpConnection t2(t1);
    noncopyable(const noncopyable&) = delete;
    noncopyable& operator = (const noncopyable&) = delete;

protected: // 限制访问权限，仅允许类内部和派生类访问。
    noncopyable() = default;
    ~noncopyable() = default;
};