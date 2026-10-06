#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

#include "dar/node/node_registry.h"

namespace dar
{

struct FailureDetectorOptions
{
    std::chrono::milliseconds scan_interval{
        std::chrono::seconds(1)};

    std::chrono::milliseconds suspect_timeout{
        std::chrono::seconds(3)};

    std::chrono::milliseconds dead_timeout{
        std::chrono::seconds(10)};
};

class FailureDetector final
{
public:
    using NodeDeadCallback =
        std::function<void(
            NodeID,
            std::uint64_t)>;

    FailureDetector(
        NodeRegistry& registry,
        FailureDetectorOptions options = {});

    ~FailureDetector();

    FailureDetector(
        const FailureDetector&) = delete;

    FailureDetector& operator=(
        const FailureDetector&) = delete;

    Status Start();

    Status Shutdown();

    void SetNodeDeadCallback(
        NodeDeadCallback callback);

    // Deterministic/manual scan used by tests and by the
    // background loop.
    void ScanOnce(
        std::chrono::steady_clock::time_point now);

private:
    void Run();

    NodeRegistry& registry_;
    FailureDetectorOptions options_;

    NodeDeadCallback node_dead_callback_;

    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace dar