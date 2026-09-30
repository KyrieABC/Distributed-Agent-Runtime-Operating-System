#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

// gRPC C++ C++ bindings header/library
#include <grpcpp/grpcpp.h>

namespace dar
{

class ConnectionPool final
{
public:
    ConnectionPool() = default;

    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    // grpc::Channel is a thread-safe object that represents a connection to a gRPC server.
    // Serve as the primary network abstraction over which client applications communicate with servers, acting as the underlying transport layer for generated client stub
    [[nodiscard]]
    std::shared_ptr<grpc::Channel> GetChannel(
        const std::string& endpoint
    );

    [[nodiscard]]
    std::size_t Size() const;

private:
    mutable std::mutex mu_;

    /**
     * A map of endpoints to gRPC channels.

    * grpc::Channel is thread-safe, wrapping it in std::shared_ptr allows multiple threads to safely hold and use a reference to the same active network connection simultaneously without worrying about manual lifetime management or race conditions during destruction
    * When caller requests a channel via GetChannel(endpoint), the pool and return a shraed ownership handle to the eixsting cached channel. Callers can you the channel as long as they need ot complete their RPCs, and the channel remains alive in the pool's cache even if individual request handlers finish and release their local copies
     */
    std::unordered_map<
        std::string,
        std::shared_ptr<grpc::Channel>
    > channels_;
};

}  // namespace dar