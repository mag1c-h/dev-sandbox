/**
 * MIT License
 *
 * Copyright (c) 2026 Mag1c.H
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * */
#include "logger.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <list>
#include <mutex>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>

class LoggerImpl {
    using Buffer = std::list<std::string>;
    using SteadyClock = std::chrono::steady_clock;
    using SystemClock = std::chrono::system_clock;
    std::atomic<bool> stop_{false};
    SteadyClock::time_point lastFlush_{SteadyClock::now()};
    Buffer frontBuf_;
    Buffer backBuf_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::thread worker_;
    static constexpr size_t FlushBatchSize = 1024;
    static constexpr auto FlushLatency = std::chrono::milliseconds(10);

    void WorkerLoop()
    {
        Buffer localBuf;
        while (true) {
            {
                std::unique_lock ul(mtx_);
                auto triggered =
                    cv_.wait_for(ul, FlushLatency, [this] { return stop_ || !backBuf_.empty(); });
                if (stop_) { break; }
                localBuf.splice(localBuf.end(), backBuf_);
                if (!triggered) { localBuf.splice(localBuf.end(), frontBuf_); }
            }
            if (localBuf.empty()) { continue; }
            Flush(localBuf);
        }
        while (!backBuf_.empty()) {
            localBuf.splice(localBuf.end(), backBuf_);
            Flush(localBuf);
        }
    }
    void Flush(Buffer& buf)
    {
        if (!buf.empty()) {
            std::string batch;
            batch.reserve(4096);
            for (const auto& s : buf) { batch += s; }
            std::fwrite(batch.data(), 1, batch.size(), stdout);
        }
        std::fflush(stdout);
        buf.clear();
    }

public:
    LoggerImpl() : worker_([this] { WorkerLoop(); }) {}
    ~LoggerImpl()
    {
        {
            std::lock_guard lg(mtx_);
            stop_ = true;
            backBuf_.splice(backBuf_.end(), frontBuf_);
            cv_.notify_one();
        }
        if (worker_.joinable()) { worker_.join(); }
    }
    void Log(Logger::Level lv, const Logger::SourceLocation& loc, const std::string& message)
    {
        static const char* lvStrs[] = {"D", "I", "W", "E", "C"};
        static const size_t pid = static_cast<size_t>(getpid());
        static thread_local const size_t tid = syscall(SYS_gettid);
        static thread_local std::chrono::seconds lastSec{0};
        static thread_local char datetime[32];
        auto systemNow = SystemClock::now();
        auto currentSec = std::chrono::time_point_cast<std::chrono::seconds>(systemNow);
        if (lastSec != currentSec.time_since_epoch()) {
            auto systemTime = SystemClock::to_time_t(systemNow);
            std::tm systemTm;
            localtime_r(&systemTime, &systemTm);
            fmt::format_to_n(datetime, sizeof(datetime), "{:%F %T}", systemTm);
            lastSec = currentSec.time_since_epoch();
        }
        auto us =
            std::chrono::duration_cast<std::chrono::microseconds>(systemNow - currentSec).count();
        auto payload = fmt::format("[{}.{:06d}][{}] {} [{},{}][{},{}:{}]\n", datetime, us,
                                   lvStrs[fmt::underlying(lv)], message, pid, tid, loc.func,
                                   basename(loc.file), loc.line);
        auto steadyNow = SteadyClock::now();
        std::lock_guard lg(mtx_);
        frontBuf_.push_back(std::move(payload));
        bool byCount = frontBuf_.size() >= FlushBatchSize;
        bool byTime = steadyNow - lastFlush_ >= FlushLatency;
        if (byCount || byTime) {
            backBuf_.splice(backBuf_.end(), frontBuf_);
            lastFlush_ = steadyNow;
            cv_.notify_one();
        }
    }
};

Logger::Logger() : impl_(std::make_unique<LoggerImpl>()) {}

Logger::~Logger() = default;

void Logger::LogInternal(Level lv, const SourceLocation& loc, const std::string& message)
{
    impl_->Log(lv, loc, message);
}
