#include "Logger.h"
#include "Timestamp.h"

#include <iostream>
#include <time.h>
#include <stdio.h>
#include <string.h>

// 全局输出和刷新函数
static Logger::OutputFunc g_output = [](const char* msg, int len) {
    fwrite(msg, 1, len, stdout);
};

static Logger::FlushFunc g_flush = []() {
    fflush(stdout);
};

void Logger::setOutput(OutputFunc out) {
    g_output = out;
}

void Logger::setFlush(FlushFunc flush) {
    g_flush = flush;
}

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

void Logger::setLogLevel(int level)
{
    logLevel_ = level;
}

void Logger::log(std::string msg)
{
    switch(logLevel_)
    {
        case INFO:
            g_output("[INFO] ", 7);
            break;
        case ERROR:
            g_output("[ERROR] ", 8);
            break;
        case FATAL:
            g_output("[FATAL] ", 8);
            break;
        case DEBUG:
            g_output("[DEBUG] ", 8);
            break;
        default:
            break;
    }

    // 获取当前时间戳并格式化
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm);
    
    g_output(timebuf, strlen(timebuf));
    g_output(" ", 1);

    // 输出消息
    g_output(msg.c_str(), msg.size());
    g_output("\n", 1);

    // 错误和致命错误立即刷盘
    if (logLevel_ == ERROR || logLevel_ == FATAL) {
        g_flush();
    }
}