#pragma once

/**
Phase 3 Stage 5I:
                    FIRST DELIVERY
ControlPlane ------------------------------> Worker
                  T1, E1

                    Worker admits E1
                    Runtime::Submit()
                          │
                          ▼
                      executes once

             response lost / deadline
ControlPlane <-------------- X


                    REDELIVERY
ControlPlane ------------------------------> Worker
                  T1, E1
                     SAME E1
 */
#include <chrono>
#include <cstdint>

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

    Status CancelTask(
        const NodeRecord& node,
        TaskID task_id,
        ExecutionID execution_id,
        std::chrono::milliseconds timeout);

private:
    // attempt 1 = original RPC
    // attempt 2 = one redelivery
    static constexpr std::size_t kMaxLaunchAttempts = 2;    

    NodeRegistry& registry_;

    RpcClient& rpc_client_;

};

}  // namespace dar