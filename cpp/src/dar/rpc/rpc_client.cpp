#include "dar/rpc/rpc_client.h"

#include <stdexcept>

namespace dar
{

RpcClient::RpcClient(ConnectionPool& connections)
    : connections_(connections)
{
}

std::shared_ptr<grpc::Channel>
RpcClient::Channel(const std::string& endpoint)
{
    // GetChannel(endpoint):
    /**
     * If found a shared_pointer with the given name(string), return the pointer
     * if not found, create a new channel, add it to the channels_ unordered_map and then return it
     */
    return connections_.GetChannel(endpoint);
}

std::unique_ptr<grpc::ClientContext>
RpcClient::CreateContext(
    std::chrono::milliseconds timeout) const
{
    if (timeout <= std::chrono::milliseconds::zero())
    {
        throw std::invalid_argument(
            "RPC timeout must be greater than zero"
        );
    }

    // std::make_unique: Safely allocates memory on the heap and returns a std::unique_ptr (guarantee there is only 1 owner of the underlying resource)
    // grpc::ClientContext: configure, control, and insepct individual RPC invocations
    // A single ClientContext is tied to only 1 specific RPC call. Two different threads or two different requests cannot share the same ClientContext - as single owner, unique_ptr is a good choice
    // -> when function executing the RPC finishes, the unique_ptr automatically cleans up the context without manual memory management
    /**
    grpc::Channel: represents the long-lived physical network connection shared acorss many requests
    grpc::ClientContext: created fresh for every single RPC call and destroyed immediately afterward. NOT thread-safe, must only be used by 1 thread/RPC at a time
    */
    auto context =
        std::make_unique<grpc::ClientContext>();

    // Put a maximum time limit on that specific RPC call
    // "RPC must finish before now + timeout", if it hasn't, return a deadline error
    context->set_deadline(
        std::chrono::system_clock::now() + timeout
    );

    // Even if context is a unique_ptr, it can be seamlessly converted and returned as a std::shared_ptr
    return context;
}

}  // namespace dar