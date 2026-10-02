#include "dar/worker/builtin_handlers.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace dar
{
namespace
{

Status BuiltinSquare(
    const TaskSpec&,
    RuntimeContext& context,
    std::string_view payload,
    std::string_view payload_media_type,
    std::string* result)
{
    if(result == nullptr)
    {
        return Status::InvalidArgument(
            "result output must not be null");
    }

    if(payload_media_type != "text/plain")
    {
        return Status::InvalidArgument(
            "builtin.square requires payload_media_type=text/plain");
    }

    if(context.IsCancellationRequested())
    {
        return Status::Cancelled(
            "builtin.square cancelled before execution");
    }

    if(payload.empty())
    {
        return Status::InvalidArgument(
            "builtin.square payload cannot be empty");
    }

    try
    {
        const std::string input(payload);

        std::size_t consumed = 0;
        // stoll: string to long long
        // 2nd parameter: a pointer to size_t where function can store the index of the first unconverted character
        const long long value = std::stoll(input, &consumed);

        if(consumed != input.size())
        {
            return Status::InvalidArgument(
                "builtin.square payload must contain one integer");
        }

        const long long squared = value * value;

        *result = std::to_string(squared);

        return Status::OK();
    }
    catch(const std::exception&)
    {
        return Status::InvalidArgument(
            "builtin.square payload must contain a valid integer");
    }
}

}  // namespace

Status RegisterBuiltinHandlers(HandlerRegistry& registry)
{
    // Initialization hook
    // BuiltinSquare has exact same return type and parameter list as the std::function RegisteredHandler
    // registry wraps BuiltinSquare into the std::function (RegisteredHandler)
    return registry.Register(
        "builtin.square",
        BuiltinSquare);
}

}  // namespace dar