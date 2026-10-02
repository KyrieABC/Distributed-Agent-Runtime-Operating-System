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

// Executable code that can be addressed by a distributed entrypoint (defines the exact shape that any pluggable task handler or command must follow so the system can invoke them dynamically)
// Unlike Phase-1 TaskHandler, this boundary receives the serialized task payload that arrived over the network.
// type alias for a callback function signature using std::function
// Strict function signature contract for any handler that wants to plug into the system
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
    // mutable: allows const member methods to lock the mutex
    // Ensure multiple threads can safely read from or write to the handlers_ map concurrently without data races
    mutable std::mutex mu_;
    // Maps the string identifier to its corresponding RegisterHandler callback function
    std::unordered_map<std::string, RegisteredHandler> handlers_;
};

}  // namespace dar