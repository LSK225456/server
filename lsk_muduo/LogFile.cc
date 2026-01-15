#include "LogFile.h"
#include "Timestamp.h"
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>

namespace lsk_muduo {

/**
 * @brief 文件封装类 - 带缓冲的文件操作
 */
class LogFile::File : noncopyable {
public:
    explicit File(const std::string& filename)
        : fp_(::fopen(filename.c_str(), "ae")),  // 'e' for O_CLOEXEC
          writtenBytes_(0) {
        assert(fp_);
        // 设置用户态缓冲区为 64KB
        ::setbuffer(fp_, buffer_, sizeof(buffer_));
    }

    ~File() {
        ::fclose(fp_);
    }

    void append(const char* logline, size_t len) {
        size_t written = 0;
        while (written != len) {
            size_t remain = len - written;
            size_t n = ::fwrite_unlocked(logline + written, 1, remain, fp_);
            if (n != remain) {
                int err = ferror(fp_);
                if (err) {
                    fprintf(stderr, "LogFile::File::append() failed %d\n", err);
                    break;
                }
            }
            written += n;
        }
        writtenBytes_ += written;
    }

    void flush() {
        ::fflush(fp_);
    }

    off_t writtenBytes() const { return writtenBytes_; }

private:
    FILE* fp_;
    char buffer_[64 * 1024];  // 64KB 用户态缓冲区
    off_t writtenBytes_;
};

// LogFile 实现
LogFile::LogFile(const std::string& basename,
                 off_t rollSize,
                 int flushInterval,
                 int checkEveryN)
    : basename_(basename),
      rollSize_(rollSize),
      flushInterval_(flushInterval),
      checkEveryN_(checkEveryN),
      count_(0),
      startOfPeriod_(0),
      lastRoll_(0),
      lastFlush_(0) {
    rollFile();
}

LogFile::~LogFile() = default;

void LogFile::append(const char* logline, int len) {
    append_unlocked(logline, len);
}

void LogFile::flush() {
    file_->flush();
}

void LogFile::append_unlocked(const char* logline, int len) {
    file_->append(logline, len);

    // 检查是否需要滚动
    if (file_->writtenBytes() > rollSize_) {
        rollFile();
    } else {
        ++count_;
        if (count_ >= checkEveryN_) {
            count_ = 0;
            time_t now = ::time(NULL);
            time_t thisPeriod = now / kRollPerSeconds_ * kRollPerSeconds_;
            
            // 跨天滚动
            if (thisPeriod != startOfPeriod_) {
                rollFile();
            }
            // 定时刷盘
            else if (now - lastFlush_ > flushInterval_) {
                lastFlush_ = now;
                file_->flush();
            }
        }
    }
}

bool LogFile::rollFile() {
    time_t now = 0;
    std::string filename = getLogFileName(basename_, &now);
    time_t start = now / kRollPerSeconds_ * kRollPerSeconds_;

    if (now > lastRoll_) {
        lastRoll_ = now;
        lastFlush_ = now;
        startOfPeriod_ = start;
        file_.reset(new File(filename));
        return true;
    }
    return false;
}

std::string LogFile::getLogFileName(const std::string& basename, time_t* now) {
    std::string filename;
    filename.reserve(basename.size() + 64);
    filename = basename;

    // 添加时间戳
    char timebuf[32];
    struct tm tm;
    *now = time(NULL);
    localtime_r(now, &tm);
    strftime(timebuf, sizeof(timebuf), ".%Y%m%d-%H%M%S", &tm);
    filename += timebuf;

    // 添加主机名
    char hostname[256];
    hostname[0] = '\0';
    if (::gethostname(hostname, sizeof(hostname)) == 0) {
        hostname[sizeof(hostname) - 1] = '\0';
        filename += '.';
        filename += hostname;
    }

    // 添加进程ID
    char pidbuf[32];
    snprintf(pidbuf, sizeof(pidbuf), ".%d", ::getpid());
    filename += pidbuf;

    filename += ".log";
    return filename;
}

} // namespace lsk_muduo
