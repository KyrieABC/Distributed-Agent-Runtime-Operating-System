#pragma once

/**
CONTROL PLANE PROCESS                   WORKER NODE PROCESS
CLIENT SIDE                             SERVER SIDE

ControlPlane
    │
    ▼
NodeManager
    │
    ▼
RpcClient
    │
    ▼
grpc::Channel
    │
    ▼
WorkerService::Stub
    │
    │
    │ stub->LaunchTask(...)
    │
    │         gRPC / network
════╪══════════════════════════════════════╪════
    │                                      │
    │                                      ▼
    │                            gRPC Server
    │                                      │
    │                                      ▼
    │                            WorkerServiceImpl
    │                                      │
    │                            LaunchTask(...)
    │                                      │
    │                                      ▼
    │                              Runtime::Submit()
    │                                      │
    │                                      ▼
    │                              Phase-1 Runtime
 */
#include <chrono>
#include <string>

#include "dar/common/id.h"
#include "dar/common/status.h"
#include "dar/core/resource.h"
#include "dar/node/node_manager.h"
#include "dar/node/node_registry.h"

namespace dar
{

struct SubmitTaskSpec
{
    TenantID tenant_id;

    AgentID agent_id;

    std::string name;

    std::string entrypoint;

    ResourceRequest resources;

    std::string payload;

    std::string payload_media_type;
};


struct SubmitTaskResult
{
    TaskID task_id;

    ExecutionID execution_id;

    NodeID node_id;
};


class ControlPlane final
{
public:
    ControlPlane(
        NodeRegistry& registry,
        NodeManager& node_manager);

    Status SubmitTask(
        const SubmitTaskSpec& submission,
        SubmitTaskResult* out);

private:
    NodeRegistry& registry_;

    NodeManager& node_manager_;
};

}  // namespace dar