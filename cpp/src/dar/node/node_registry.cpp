#include "dar/node/node_registry.h"

#include <algorithm>
#include <utility>

namespace dar
{

/**
 Registry has nothing:
    Register(N1, 1) → OK

Registry has N1/1:
    Register(N1, 1) identical → OK

Registry has N1/1:
    Register(N1, 1) conflicting → FailedPrecondition

Registry has N1/1:
    Register(N1, 2) → OK, replace lifetime

Registry has N1/2:
    Register(N1, 1) → FailedPrecondition
 */
Status NodeRegistry::RegisterNode(NodeRecord node)
{
    std::lock_guard<std::mutex> lock(mu_);

    if(node.node_id.IsNil())
    {
        return Status::InvalidArgument(
            "node id cannot be nil");
    }

    if(node.worker_endpoint.empty())
    {
        return Status::InvalidArgument(
            "worker endpoint cannot be empty");
    }

    if(node.incarnation == 0)
    {
        return Status::InvalidArgument(
            "node incarnation must be greater than zero");
    }

    const NodeID node_id =
        node.node_id;

    auto it =
        nodes_.find(node_id);

    // --------------------------------------------------------
    // Brand-new NodeID
    // --------------------------------------------------------

    if(it == nodes_.end())
    {
        node.schedulable_available =
            node.reported_available;

        node.last_heartbeat_received =
            std::chrono::steady_clock::now();

        nodes_.emplace(
            node_id,
            StoredNode(std::move(node)));

        registration_order_.push_back(
            node_id);

        return Status::OK();
    }

    const std::uint64_t stored_incarnation =
        it->second.record.incarnation;

    // --------------------------------------------------------
    // Stale registration
    //
    // Example:
    //
    // stored   = (N1, incarnation 2)
    // incoming = (N1, incarnation 1)
    // --------------------------------------------------------

    if(node.incarnation < stored_incarnation)
    {
        return Status::FailedPrecondition(
            "stale node incarnation");
    }

    // --------------------------------------------------------
    // Duplicate registration of the SAME lifetime.
    //
    // Registration is idempotent.
    //
    // Do NOT reset the ResourcePool here. The control plane
    // may already have reservations against this incarnation.
    // --------------------------------------------------------

    if(node.incarnation == stored_incarnation)
    {
        const NodeRecord& existing =
            it->second.record;

        if(
            existing.worker_endpoint !=
                node.worker_endpoint ||
            existing.total_resources !=
                node.total_resources)
        {
            return Status::FailedPrecondition(
                "conflicting registration for "
                "current node incarnation");
        }

        return Status::OK();
    }

    // --------------------------------------------------------
    // Newer incarnation.
    //
    // Same logical NodeID, but a new process lifetime.
    //
    // Phase 5D only replaces the registration.
    //
    // Handling executions belonging to the dead incarnation
    // belongs to failure handling in 5F.
    // --------------------------------------------------------

    node.schedulable_available =
        node.reported_available;

    node.last_heartbeat_received =
        std::chrono::steady_clock::now();

    it->second =
        StoredNode(std::move(node));

    // Keep the original registration-order position.
    // Do NOT push node_id into registration_order_ again.

    return Status::OK();
}

Status NodeRegistry::Heartbeat(
    NodeID node_id,
    std::uint64_t incarnation,
    const ResourceSet& reported_available,
    std::uint32_t running_tasks)
{
    if(node_id.IsNil())
    {
        return Status::InvalidArgument(
            "heartbeat node id cannot be nil");
    }

    if(incarnation == 0)
    {
        return Status::InvalidArgument(
            "heartbeat incarnation must be greater than zero");
    }

    std::lock_guard<std::mutex> lock(mu_);

    auto it =
        nodes_.find(node_id);

    if(it == nodes_.end())
    {
        return Status::NotFound(
            "heartbeat node is not registered");
    }

    StoredNode& stored =
        it->second;

    if(stored.record.incarnation != incarnation)
    {
        return Status::FailedPrecondition(
            "heartbeat node incarnation does not match");
    }

    /**
    SUSPECT + heartbeat → ALIVE
    DEAD    + heartbeat → reject
    DEAD    + newer incarnation registration → ALIVE
     */
    if(stored.record.state == NodeState::kDead)
    {
        return Status::FailedPrecondition(
        "dead node incarnation cannot be revived by heartbeat");
    } 

    stored.record.reported_available =
        reported_available;

    stored.record.running_tasks =
        running_tasks;

    stored.record.last_heartbeat_received =
        std::chrono::steady_clock::now();

    // A valid heartbeat from the current lifetime revives a
    // SUSPECT node. A DEAD lifetime cannot reach this point.
    stored.record.state = NodeState::kAlive;

    // Do NOT modify schedulable_available or schedulable_pool here.

    return Status::OK();
}

std::vector<DeadNodeLifetime>
NodeRegistry::UpdateLiveness(
    std::chrono::steady_clock::time_point now,
    std::chrono::milliseconds suspect_timeout,
    std::chrono::milliseconds dead_timeout)
{
    std::vector<DeadNodeLifetime> newly_dead;

    if(suspect_timeout.count() < 0 ||
       dead_timeout.count() < 0 ||
       suspect_timeout >= dead_timeout)
    {
        return newly_dead;
    }

    std::lock_guard<std::mutex> lock(mu_);

    for(const NodeID& node_id : registration_order_)
    {
        auto it = nodes_.find(node_id);

        if(it == nodes_.end())
        {
            continue;
        }

        NodeRecord& node =
            it->second.record;

        // DEAD is terminal for this incarnation.
        if(node.state == NodeState::kDead)
        {
            continue;
        }

        const auto elapsed =
            now - node.last_heartbeat_received;

        if(elapsed >= dead_timeout)
        {
            node.state =
                NodeState::kDead;

            newly_dead.push_back(
                DeadNodeLifetime{
                    node.node_id,
                    node.incarnation});

            continue;
        }

        if(elapsed >= suspect_timeout)
        {
            node.state =
                NodeState::kSuspect;

            continue;
        }

        node.state =
            NodeState::kAlive;
    }

    return newly_dead;
}

Status NodeRegistry::RemoveNode(
    NodeID node_id,
    std::uint64_t incarnation)
{
    std::lock_guard<std::mutex> lock(mu_);

    const auto it = nodes_.find(node_id);

    if(it == nodes_.end())
    {
        return Status::NotFound(
            "node is not registered");
    }

    // node incarnation: version number of a particular lifetime of a node
    // (NodeID, incarnation) identifies a specific lifetime of that node
    /**
     * 1. Node B starts
   NodeID = B
   incarnation = 1

2. ControlPlane registers B

3. B crashes

4. B restarts
   NodeID = B
   incarnation = 2

5. An OLD delayed message arrives:
   "Remove Node B, incarnation 1"
     */
    if(it->second.record.incarnation != incarnation)
    {
        return Status::FailedPrecondition(
            "node incarnation does not match");
    }

    nodes_.erase(it);

    // std::remove() from <algorithm>:
    // Moves all elements that do not match the target value to the front of the range and returns an iterator to the new logical end
    /**
     * std::vector<int> v = {1, 2, 3, 4, 3, 5};
     * v.erase(std::remove(v.begin(), v.end(), 3), v.end());
     */
    registration_order_.erase(
        std::remove(
            registration_order_.begin(),
            registration_order_.end(),
            node_id),
        registration_order_.end());

    return Status::OK();
}


Status NodeRegistry::GetNode(
    NodeID node_id,
    NodeRecord* out) const
{
    if(out == nullptr)
    {
        return Status::InvalidArgument(
            "node output must not be null");
    }

    std::lock_guard<std::mutex> lock(mu_);

    const auto it = nodes_.find(node_id);

    if(it == nodes_.end())
    {
        return Status::NotFound(
            "node is not registered");
    }

    *out = it->second.record;

    return Status::OK();
}

// Return a vector(actual information about every node)
std::vector<NodeRecord> NodeRegistry::Nodes() const
{
    std::lock_guard<std::mutex> lock(mu_);

    std::vector<NodeRecord> result;

    // vector::reserve(): pre-allocate memory for at least n element without changing actual size of vector
    // prevent expensive memory reallocation and element copying while appending data
    result.reserve(registration_order_.size());

    for(const NodeID& node_id : registration_order_)
    {
        const auto it = nodes_.find(node_id);

        if(it != nodes_.end())
        {
            result.push_back(it->second.record);
        }
    }

    return result;
}


Status NodeRegistry::ReserveFirstFit(
    const ResourceRequest& request,
    NodeRecord* out)
{
    if(out == nullptr)
    {
        return Status::InvalidArgument(
            "node output must not be null");
    }

    std::lock_guard<std::mutex> lock(mu_);

    for(const NodeID& node_id : registration_order_)
    {
        auto it = nodes_.find(node_id);

        if(it == nodes_.end())
        {
            continue;
        }

        StoredNode& node = it->second;

        if(node.record.state != NodeState::kAlive)
        {
            continue;
        }

        if(!node.schedulable_pool.CanFit(request))
        {
            continue;
        }

        const Status acquire_status =
            node.schedulable_pool.Acquire(request);

        if(!acquire_status.ok())
        {
            return Status::Internal(
                "schedulable resource reservation failed");
        }

        // Available(): returns a ResourceSet object of available resources
        node.record.schedulable_available =
            node.schedulable_pool.Available();

        *out = node.record;

        return Status::OK();
    }

    return Status::ResourceExhausted(
        "no schedulable node can satisfy resource request");
}


Status NodeRegistry::Release(
    NodeID node_id,
    const ResourceRequest& request)
{
    std::lock_guard<std::mutex> lock(mu_);

    auto it = nodes_.find(node_id);

    if(it == nodes_.end())
    {
        return Status::NotFound(
            "node is not registered");
    }

    Status status =
        it->second.schedulable_pool.Release(request);

    if(!status.ok())
    {
        return status;
    }

    it->second.record.schedulable_available =
        it->second.schedulable_pool.Available();

    return Status::OK();
}

}  // namespace dar