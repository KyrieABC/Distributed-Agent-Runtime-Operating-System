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

    auto context =
        std::make_unique<grpc::ClientContext>();

    context->set_deadline(
        std::chrono::system_clock::now() + timeout
    );

    return context;
}

}  // namespace dar