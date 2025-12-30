#pragma once

class noncopyable
{
public:
    noncopyable(const noncopyable&) = delete;
    noncopyable& operator = (const noncopyable&) = delete;
private:
    noncopyable() = default;
    ~noncopyable() = default;
};