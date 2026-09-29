#pragma once

#include <chrono>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>

#include "dar/rpc/connection_pool.h"

namespace dar
{

class RpcClient final
{
public:
    explicit RpcClient(ConnectionPool& connections);

    RpcClient(const RpcClient&) = delete;
    RpcClient& operator=(const RpcClient&) = delete;

    [[nodiscard]]
    std::shared_ptr<grpc::Channel> Channel(
        const std::string& endpoint
    );

    [[nodiscard]]
    std::unique_ptr<grpc::ClientContext> CreateContext(
        std::chrono::milliseconds timeout
    ) const;

private:
    ConnectionPool& connections_;
};

}  // namespace dar