#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <grpcpp/grpcpp.h>

namespace dar
{

class ConnectionPool final
{
public:
    ConnectionPool() = default;

    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    [[nodiscard]]
    std::shared_ptr<grpc::Channel> GetChannel(
        const std::string& endpoint
    );

    [[nodiscard]]
    std::size_t Size() const;

private:
    mutable std::mutex mu_;

    std::unordered_map<
        std::string,
        std::shared_ptr<grpc::Channel>
    > channels_;
};

}  // namespace dar