/**
 * Same endpoint -> same shared Channel
 */

#include "dar/rpc/connection_pool.h"

namespace dar
{

std::shared_ptr<grpc::Channel>
ConnectionPool::GetChannel(const std::string& endpoint)
{
    std::lock_guard<std::mutex> lock(mu_);

    const auto it = channels_.find(endpoint);

    if (it != channels_.end())
    {
        return it->second;
    }

    auto channel = grpc::CreateChannel(
        endpoint,
        grpc::InsecureChannelCredentials()
    );

    channels_.emplace(endpoint, channel);

    return channel;
}

std::size_t ConnectionPool::Size() const
{
    std::lock_guard<std::mutex> lock(mu_);
    return channels_.size();
}

}  // namespace dar