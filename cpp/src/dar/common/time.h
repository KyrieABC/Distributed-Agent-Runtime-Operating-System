#pragma once

#include <chrono>

namespace dar
{
    // wall-clock timestamp used for externally meaningful event times such as execution createion, start, and completion
    // system-clock is appropriate here because these timestamps may eventually be persisted, serialized, loggedd, or compared across processes

    using WallTime = std::chrono::system_clock::time_point;
    [[nodiscard]] inline WallTime WallTimeNow() noexcept
    {
        return std::chrono::system_clock::now();
    }
}