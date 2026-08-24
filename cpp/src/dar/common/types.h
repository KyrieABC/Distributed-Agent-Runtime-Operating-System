#pragma once

#include <cstdint>

namespace dar
{
    // Execution attempt are numberred start from 1
    // A retry creates a new Execution with the same TaskID, a new ExecutionID and an incremented Attempt number
    using AttemptNumber = std::uint32_t;
    // This is only an alias, does not create distinct c++ types
    // Make it strongly typed later. 
}