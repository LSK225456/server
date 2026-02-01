#pragma once

#include "noncopyable.h"
#include <string>
#include <memory>
#include <mutex>
#include <cstdio>

namespace lsk_muduo {

// 文件写入封装类
class AppendFile : noncopyable {
public:
    explicit AppendFile(const std::string& filename);
    ~AppendFile();

    void append(const char* logline, size_t len);
    void flush();
    off_t writtenBytes() const { return writtenBytes_; }

private:
    size_t write(const char* logline, size_t len);

    FILE* fp_;
    char buffer_[64 * 1024];  // 64KB 用户态缓冲区
    off_t writtenBytes_;
};

// 日志文件类（支持滚动）
class LogFile : noncopyable {
public:
    LogFile(const std::string& basename,
            off_t rollSize,
            bool threadSafe = true,
            int flushInterval = 3,
            int checkEveryN = 1024);
    ~LogFile();

    void append(const char* logline, int len);
    void flush();
    bool rollFile();

private:
    void append_unlocked(const char* logline, int len);
    static std::string getLogFileName(const std::string& basename, time_t* now);

    const std::string basename_;
    const off_t rollSize_;
    const int flushInterval_;
    const int checkEveryN_;

    int count_;

    std::unique_ptr<std::mutex> mutex_;
    time_t startOfPeriod_;  // 当天零点时间
    time_t lastRoll_;
    time_t lastFlush_;
    std::unique_ptr<AppendFile> file_;

    const static int kRollPerSeconds_ = 60 * 60 * 24;  // 每天滚动
};

} // namespace lsk_muduo
