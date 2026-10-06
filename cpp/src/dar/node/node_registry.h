#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <cstdint>

#include "dar/common/id.h"
#include "dar/common/status.h"
#include "dar/core/resource.h"

namespace dar
{

enum class NodeState
{
    kAlive,
    kSuspect,
    kDead
};

struct NodeRecord
{
    NodeID node_id;

    std::string worker_endpoint;

    ResourceSet total_resources;

    // Last capacity reported by the worker.
    ResourceSet reported_available;

    // Control-plane-owned reservation ledger.
    ResourceSet schedulable_available;

    std::uint64_t incarnation{0};

    NodeState state{NodeState::kAlive};

    // Last worker-reported running task count.
    std::uint32_t running_tasks{0};

    // LOCAL receipt time of the most recently accepted heartbeat.
    //
    // This uses steady_clock deliberately. Failure detection must not
    // depend on synchronization between worker and control-plane clocks.
    std::chrono::steady_clock::time_point last_heartbeat_received{};
};

class NodeRegistry final
{
public:
    NodeRegistry() = default;

    NodeRegistry(const NodeRegistry&) = delete;
    NodeRegistry& operator=(const NodeRegistry&) = delete;

    Status RegisterNode(NodeRecord node);

    Status RemoveNode(
        NodeID node_id,
        std::uint64_t incarnation);
    
    Status Heartbeat(
        NodeID node_id,
        std::uint64_t incarnation,
        const ResourceSet& reported_available,
        std::uint32_t running_tasks);

    Status GetNode(
        NodeID node_id,
        NodeRecord* out) const;

    [[nodiscard]]
    std::vector<NodeRecord> Nodes() const;

    // Atomically:
    //
    // 1. scan ALIVE nodes in deterministic registration order
    // 2. find the first node that can satisfy request
    // 3. reserve its schedulable capacity
    // 4. return that node
    //
    // Selection + reservation happen under the same lock.
    Status ReserveFirstFit(
        const ResourceRequest& request,
        NodeRecord* out);

    Status Release(
        NodeID node_id,
        const ResourceRequest& request);

private:
    struct StoredNode
    {
        NodeRecord record;

        // ResourcePool owns the actual reservation arithmetic.
        ResourcePool schedulable_pool;

        explicit StoredNode(NodeRecord node)
            : record(std::move(node)),
              schedulable_pool(
                  record.schedulable_available)
        {
        }
    };

    mutable std::mutex mu_;

    std::unordered_map<
        NodeID,
        StoredNode,
        StrongIDHash<NodeID>>
        nodes_;

    // unordered_map iteration is not deterministic.
    // Preserve registration order for first-fit.
    std::vector<NodeID> registration_order_;
};

}  // namespace dar