#pragma once

#include "noncopyable.h"
#include <memory>
#include <string>

namespace lsk_muduo {

/**
 * @brief 日志文件类 - 支持滚动功能
 * 
 * 滚动策略：
 * 1. 按文件大小滚动（默认 1GB）
 * 2. 按时间滚动（默认每天零点）
 * 3. 每写 N 次检查一次（默认 1024 次）
 * 
 * 文件命名：basename.20240101-120000.hostname.pid.log
 */
class LogFile : noncopyable {
public:
    /**
     * @param basename 日志文件基础名
     * @param rollSize 滚动大小（字节），默认 1GB
     * @param flushInterval 刷盘间隔（秒），默认 3 秒
     * @param checkEveryN 每写 N 次检查是否需要滚动，默认 1024
     */
    LogFile(const std::string& basename,
            off_t rollSize = 1024 * 1024 * 1024,
            int flushInterval = 3,
            int checkEveryN = 1024);

    ~LogFile();

    // 追加日志内容
    void append(const char* logline, int len);
    
    // 手动刷盘
    void flush();
    
    // 手动触发滚动
    bool rollFile();

private:
    // 生成日志文件名
    static std::string getLogFileName(const std::string& basename, time_t* now);
    
    // 追加内部实现（无锁）
    void append_unlocked(const char* logline, int len);

    const std::string basename_;      // 日志文件基础名
    const off_t rollSize_;            // 滚动文件大小
    const int flushInterval_;         // 刷盘间隔（秒）
    const int checkEveryN_;           // 检查滚动频率

    int count_;                       // 写入计数
    time_t startOfPeriod_;            // 当前文件所属时间段（天）
    time_t lastRoll_;                 // 上次滚动时间
    time_t lastFlush_;                // 上次刷盘时间

    class File;                       // 前向声明
    std::unique_ptr<File> file_;      // 文件封装

    const static int kRollPerSeconds_ = 60 * 60 * 24; // 每天滚动一次
};

} // namespace lsk_muduo
