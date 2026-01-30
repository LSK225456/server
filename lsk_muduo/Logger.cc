#include "Logger.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/time.h>
#include <cstdlib>
#include <errno.h>

namespace lsk_muduo {

// 全局日志级别
Logger::LogLevel g_logLevel = Logger::INFO;

const char* LogLevelName[Logger::NUM_LOG_LEVELS] = {
    "TRACE ",
    "DEBUG ",
    "INFO  ",
    "WARN  ",
    "ERROR ",
    "FATAL ",
};

// 默认输出到 stdout
static void defaultOutput(const char* msg, int len) {
    fwrite(msg, 1, len, stdout);
}

static void defaultFlush() {
    fflush(stdout);
}

static Logger::OutputFunc g_output = defaultOutput;
static Logger::FlushFunc g_flush = defaultFlush;

void Logger::setOutput(OutputFunc out) {
    g_output = out;
}

void Logger::setFlush(FlushFunc flush) {
    g_flush = flush;
}

void Logger::setLogLevel(LogLevel level) {
    g_logLevel = level;
}

Logger::Logger(SourceFile file, int line)
    : stream_(),
      level_(INFO),
      line_(line),
      basename_(file) {
    formatTime();
    stream_ << LogLevelName[level_];
}

Logger::Logger(SourceFile file, int line, LogLevel level)
    : stream_(),
      level_(level),
      line_(line),
      basename_(file) {
    formatTime();
    stream_ << LogLevelName[level_];
}

Logger::Logger(SourceFile file, int line, LogLevel level, const char* func)
    : stream_(),
      level_(level),
      line_(line),
      basename_(file) {
    formatTime();
    stream_ << LogLevelName[level_] << func << ' ';
}

Logger::Logger(SourceFile file, int line, bool toAbort)
    : stream_(),
      level_(toAbort ? FATAL : ERROR),
      line_(line),
      basename_(file) {
    formatTime();
    stream_ << LogLevelName[level_];
    // 输出 errno 信息
    int savedErrno = errno;
    if (savedErrno != 0) {
        stream_ << strerror(savedErrno) << " (errno=" << savedErrno << ") ";
    }
}

void Logger::formatTime() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    time_t seconds = tv.tv_sec;
    struct tm tm_time;
    localtime_r(&seconds, &tm_time);

    char buf[64];
    int len = snprintf(buf, sizeof(buf), "%4d%02d%02d %02d:%02d:%02d.%06ld ",
                       tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
                       tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec,
                       tv.tv_usec);
    stream_.append(buf, len);
}

void Logger::finish() {
    stream_ << " - " << basename_.data_ << ':' << line_ << '\n';
}

Logger::~Logger() {
    finish();
    const LogStream::Buffer& buf = stream_.buffer();
    g_output(buf.data(), buf.length());
    if (level_ == FATAL) {
        g_flush();
        abort();
    }
}

} // namespace lsk_muduo