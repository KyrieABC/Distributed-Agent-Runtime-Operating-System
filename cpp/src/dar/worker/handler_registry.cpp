#include "dar/worker/handler_registry.h"

#include <utility>

namespace dar
{

Status HandlerRegistry::Register(
    std::string entrypoint,
    RegisteredHandler handler)
{
    if(entrypoint.empty())
    {
        return Status::InvalidArgument(
            "handler entrypoint cannot be empty");
    }

    if(!handler)
    {
        return Status::InvalidArgument(
            "registered handler cannot be empty");
    }

    std::lock_guard<std::mutex> lock(mu_);

    // constructs an element directly in-place inside map (avoid unnecessary copies)
    const auto [it, inserted] = handlers_.emplace(
        std::move(entrypoint),
        std::move(handler));

    if(!inserted)
    {
        return Status::AlreadyExists(
            "handler entrypoint is already registered");
    }

    return Status::OK();
}

// Perform lookup from a distributed function name to actual local C++ executable code
Status HandlerRegistry::Resolve(
    const std::string& entrypoint,
    RegisteredHandler* out) const
{
    if(out == nullptr)
    {
        return Status::InvalidArgument(
            "handler output must not be null");
    }

    std::lock_guard<std::mutex> lock(mu_);

    const auto it = handlers_.find(entrypoint);

    if(it == handlers_.end())
    {
        return Status::NotFound(
            "handler entrypoint is not registered: " + entrypoint);
    }

    *out = it->second;

    return Status::OK();
}

bool HandlerRegistry::Contains(
    const std::string& entrypoint) const
{
    std::lock_guard<std::mutex> lock(mu_);
    return handlers_.find(entrypoint) != handlers_.end();
}

}  // namespace dar