#pragma once

#include <chrono>

#include "dar/common/status.h"
#include "dar/node/node_registry.h"
#include "dar/rpc/rpc_client.h"

#include "task.pb.h"

namespace dar
{

class NodeManager final
{
public:
    NodeManager(
        NodeRegistry& registry,
        RpcClient& rpc_client);

    NodeManager(const NodeManager&) = delete;
    NodeManager& operator=(const NodeManager&) = delete;

    Status RegisterNode(NodeRecord node);

    Status Heartbeat(
        NodeID node_id,
        std::uint64_t incarnation,
        const ResourceSet& reported_available,
        std::uint32_t running_tasks);

    Status DispatchTask(
        const NodeRecord& node,
        const proto::v1::TaskDescriptor& task,
        std::chrono::milliseconds timeout);

private:
    NodeRegistry& registry_;

    RpcClient& rpc_client_;
};

}  // namespace dar