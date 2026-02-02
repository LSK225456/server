#pragma once

#include "noncopyable.h"
#include "LogStream.h"
#include <functional>

// 防止系统宏与枚举值冲突
#ifdef DEBUG
#undef DEBUG
#endif

namespace lsk_muduo {

class Logger : noncopyable {
public:
    enum LogLevel {
        TRACE,
        DEBUG,
        INFO,
        WARN,
        ERROR,
        FATAL,
        NUM_LOG_LEVELS,
    };

    // 编译期获取文件名
    class SourceFile {
    public:
        template<int N>
        SourceFile(const char (&arr)[N]) : data_(arr), size_(N - 1) {
            const char* slash = strrchr(data_, '/');
            if (slash) {
                data_ = slash + 1;
                size_ -= static_cast<int>(data_ - arr);
            }
        }

        explicit SourceFile(const char* filename) : data_(filename) {
            const char* slash = strrchr(filename, '/');
            if (slash) {
                data_ = slash + 1;
            }
            size_ = static_cast<int>(strlen(data_));
        }

        const char* data_;
        int size_;
    };

    Logger(SourceFile file, int line);
    Logger(SourceFile file, int line, LogLevel level);
    Logger(SourceFile file, int line, LogLevel level, const char* func);
    Logger(SourceFile file, int line, bool toAbort);
    ~Logger();

    LogStream& stream() { return stream_; }

    static LogLevel logLevel();
    static void setLogLevel(LogLevel level);

    using OutputFunc = std::function<void(const char* msg, int len)>;
    using FlushFunc = std::function<void()>;
    static void setOutput(OutputFunc);
    static void setFlush(FlushFunc);

private:
    void formatTime();
    void finish();

    LogStream stream_;
    LogLevel level_;
    int line_;
    SourceFile basename_;
};

extern Logger::LogLevel g_logLevel;

inline Logger::LogLevel Logger::logLevel() {
    return g_logLevel;
}

} // namespace lsk_muduo

// 流式日志宏
#define LOG_TRACE if (lsk_muduo::Logger::logLevel() <= lsk_muduo::Logger::TRACE) \
    lsk_muduo::Logger(__FILE__, __LINE__, lsk_muduo::Logger::TRACE, __func__).stream()
#define LOG_DEBUG if (lsk_muduo::Logger::logLevel() <= lsk_muduo::Logger::DEBUG) \
    lsk_muduo::Logger(__FILE__, __LINE__, lsk_muduo::Logger::DEBUG, __func__).stream()
#define LOG_INFO if (lsk_muduo::Logger::logLevel() <= lsk_muduo::Logger::INFO) \
    lsk_muduo::Logger(__FILE__, __LINE__).stream()
#define LOG_WARN lsk_muduo::Logger(__FILE__, __LINE__, lsk_muduo::Logger::WARN).stream()
#define LOG_ERROR lsk_muduo::Logger(__FILE__, __LINE__, lsk_muduo::Logger::ERROR).stream()
#define LOG_FATAL lsk_muduo::Logger(__FILE__, __LINE__, lsk_muduo::Logger::FATAL).stream()
#define LOG_SYSERR lsk_muduo::Logger(__FILE__, __LINE__, false).stream()
#define LOG_SYSFATAL lsk_muduo::Logger(__FILE__, __LINE__, true).stream()