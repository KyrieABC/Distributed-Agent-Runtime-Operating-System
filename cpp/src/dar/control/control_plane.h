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
#include <mutex>
#include <unordered_map>
#include <utility>

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

enum class DistributedTaskState
{
    KRunning,
    KSucceeded,
    KFailed,
    KCanceled
};

struct DistributedTaskRecord
{
    TaskID task_id;

    ExecutionID execution_id;

    NodeID node_id;

    ResourceRequest resources;

    DistributedTaskState state{DistributedTaskState::KRunning};

    Status terminal_status;

    std::string result;

    std::string result_media_type;
};

class ControlPlane final
{
public:
    ControlPlane(
        NodeRegistry& registry,
        NodeManager& node_manager);

    Status RegisterNode(NodeRecord node);

    Status RemoveNode(NodeID node_id, std::uint64_t incarnation);

    Status Heartbeat(
        NodeID node_id,
        std::uint64_t incarnation,
        const ResourceSet& reported_available,
        std::uint32_t running_tasks);

    Status SubmitTask(
        const SubmitTaskSpec& submission,
        SubmitTaskResult* out);

    Status ReportTaskResult(
        TaskID task_id,
        ExecutionID execution_id);

    Status ReportTaskResult(
        TaskID task_id,
        ExecutionID execution_id,
        DistributedTaskState state,
        Status terminal_state,
        std::string result,
        std::string result_media_type
    );

    Status GetTaskRecord(
        TaskID task_id,
        DistributedTaskRecord* out
    ) const;
private:
    NodeRegistry& registry_;

    NodeManager& node_manager_;

    mutable std::mutex tasks_mu_;

    std::unordered_map<TaskID, DistributedTaskRecord, StrongIDHash<TaskID>> tasks_;
};

}  // namespace dar