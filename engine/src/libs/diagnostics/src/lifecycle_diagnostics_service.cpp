#include "lifecycle_diagnostics_service.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>
#include <spdlog/spdlog.h>
#include "fs.h"
#include "spdlog_sinks/syncable_sink.hpp"

namespace storm::diag
{
class LoggingService final
{
public:
    ~LoggingService() { terminate(); }

    void initialize()
    {
        if (terminate_)
        {
            terminate_ = false;
            create_directories(fs::GetLogsPath());
            worker_ = std::thread([this] { loggingThread(); });
        }
    }

    void terminate()
    {
        if (!terminate_.exchange(true))
        {
            {
                std::lock_guard lock(mtx_);
                flushRequested_ = true;
            }
            cv_.notify_one();
            if (worker_.joinable()) worker_.join();
            spdlog::shutdown();
        }
    }

    void flushAsync()
    {
        {
            std::lock_guard lock(mtx_);
            flushRequested_ = true;
        }
        cv_.notify_one();
    }

private:
    std::mutex mtx_;
    std::condition_variable cv_;
    bool flushRequested_ = false;
    std::atomic_bool terminate_{true};
    std::thread worker_;

    void loggingThread()
    {
        for (;;)
        {
            bool terminating;
            {
                std::unique_lock lock(mtx_);
                cv_.wait(lock, [this] { return flushRequested_; });
                flushRequested_ = false;
                terminating = terminate_;
            }
            spdlog::apply_all([terminating](std::shared_ptr<spdlog::logger> logger) {
                logger->flush();
                if (terminating)
                    for (auto &sink : logger->sinks())
                        if (const auto syncable = std::dynamic_pointer_cast<logging::sinks::syncable_sink>(sink))
                            syncable->terminate_immediately();
            });
            if (terminating) return;
        }
    }
};

LifecycleDiagnosticsService::LifecycleDiagnosticsService() : loggingService_(std::make_unique<LoggingService>()) {}
LifecycleDiagnosticsService::~LifecycleDiagnosticsService() { terminate(); }
LifecycleDiagnosticsService::Guard LifecycleDiagnosticsService::initialize(bool)
{
    loggingService_->initialize();
    initialized_ = true;
    return Guard(*this);
}
void LifecycleDiagnosticsService::terminate() const { loggingService_->terminate(); }
void LifecycleDiagnosticsService::notifyAfterRun() const
{
    static auto latest = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (now - latest >= getLoggingFlushPeriod())
    {
        loggingService_->flushAsync();
        latest = now;
    }
}
void LifecycleDiagnosticsService::setCrashInfoCollector(crash_info_collector collector)
{
    collectCrashInfo_ = std::move(collector);
}
}
