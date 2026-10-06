#include "dar/node/failure_detector.h"

#include <utility>

namespace dar
{

FailureDetector::FailureDetector(
    NodeRegistry& registry,
    FailureDetectorOptions options)
    : registry_(registry),
      options_(std::move(options))
{
}

FailureDetector::~FailureDetector()
{
    Shutdown();
}

Status FailureDetector::Start()
{
    if(options_.scan_interval.count() <= 0)
    {
        return Status::InvalidArgument(
            "failure detector scan interval must be positive");
    }

    if(options_.suspect_timeout.count() <= 0)
    {
        return Status::InvalidArgument(
            "failure detector suspect timeout must be positive");
    }

    if(options_.dead_timeout <=
       options_.suspect_timeout)
    {
        return Status::InvalidArgument(
            "failure detector dead timeout must exceed suspect timeout");
    }

    bool expected = false;

    if(!running_.compare_exchange_strong(
           expected,
           true))
    {
        return Status::FailedPrecondition(
            "failure detector is already running");
    }

    thread_ =
        std::thread(
            &FailureDetector::Run,
            this);

    return Status::OK();
}

Status FailureDetector::Shutdown()
{
    const bool was_running =
        running_.exchange(false);

    if(!was_running)
    {
        return Status::OK();
    }

    if(thread_.joinable())
    {
        thread_.join();
    }

    return Status::OK();
}

void FailureDetector::SetNodeDeadCallback(
    NodeDeadCallback callback)
{
    node_dead_callback_ =
        std::move(callback);
}

void FailureDetector::ScanOnce(
    std::chrono::steady_clock::time_point now)
{
    const std::vector<DeadNodeLifetime> newly_dead =
        registry_.UpdateLiveness(
            now,
            options_.suspect_timeout,
            options_.dead_timeout);

    if(!node_dead_callback_)
    {
        return;
    }

    for(const DeadNodeLifetime& dead :
        newly_dead)
    {
        node_dead_callback_(
            dead.node_id,
            dead.incarnation);
    }
}

void FailureDetector::Run()
{
    while(running_.load())
    {
        ScanOnce(
            std::chrono::steady_clock::now());

        std::this_thread::sleep_for(
            options_.scan_interval);
    }
}

}  // namespace dar