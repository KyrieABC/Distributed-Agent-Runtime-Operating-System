#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "dar/common/status.h"
#include "dar/core/task_spec.h"
#include "dar/runtime/runtime_context.h"

namespace dar
{

// Executable code that can be addressed by a distributed entrypoint.
//
// Unlike Phase-1 TaskHandler, this boundary receives the serialized
// task payload that arrived over the network.
using RegisteredHandler = std::function<Status(
    const TaskSpec&,
    RuntimeContext&,
    std::string_view payload,
    std::string_view payload_media_type,
    std::string* result)>;

class HandlerRegistry final
{
public:
    HandlerRegistry() = default;

    HandlerRegistry(const HandlerRegistry&) = delete;
    HandlerRegistry& operator=(const HandlerRegistry&) = delete;

    // Register one local implementation under a stable distributed name.
    Status Register(std::string entrypoint, RegisteredHandler handler);

    // Resolve distributed code identity -> local executable implementation.
    Status Resolve(
        const std::string& entrypoint,
        RegisteredHandler* out) const;

    [[nodiscard]] bool Contains(const std::string& entrypoint) const;

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, RegisteredHandler> handlers_;
};

}  // namespace dar