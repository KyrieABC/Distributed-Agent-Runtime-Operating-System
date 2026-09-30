#pragma once

// Data serialization means: converting data structures to a format that can be stored or transmitted and reconstructed later. This file contains functions for serializing and deserializing common DAR types to and from protocol buffer messages.
#include "dar/common/id.h"
#include "dar/common/status.h"
#include "dar/core/resource.h"

#include "common.pb.h"

namespace dar::serialization
{

// ============================================================
// Strong IDs
// ============================================================

void ToProto(const AgentID& id, proto::v1::AgentId* out);
void ToProto(const TaskID& id, proto::v1::TaskId* out);
void ToProto(const ExecutionID& id, proto::v1::ExecutionId* out);
void ToProto(const ObjectID& id, proto::v1::ObjectId* out);
void ToProto(const WorkerID& id, proto::v1::WorkerId* out);
void ToProto(const NodeID& id, proto::v1::NodeId* out);
void ToProto(const TenantID& id, proto::v1::TenantId* out);
void ToProto(const DeploymentID& id, proto::v1::DeploymentId* out);

Status FromProto(const proto::v1::AgentId& proto_id, AgentID* out);
Status FromProto(const proto::v1::TaskId& proto_id, TaskID* out);
Status FromProto(const proto::v1::ExecutionId& proto_id, ExecutionID* out);
Status FromProto(const proto::v1::ObjectId& proto_id, ObjectID* out);
Status FromProto(const proto::v1::WorkerId& proto_id, WorkerID* out);
Status FromProto(const proto::v1::NodeId& proto_id, NodeID* out);
Status FromProto(const proto::v1::TenantId& proto_id, TenantID* out);
Status FromProto(const proto::v1::DeploymentId& proto_id, DeploymentID* out);

// ============================================================
// Status
// ============================================================

void ToProto(const Status& status, proto::v1::Status* out);

Status FromProtoStatus(const proto::v1::Status& status);

// ============================================================
// Resources
// ============================================================

void ToProto(const ResourceSet& resources, proto::v1::ResourceSet* out);

Status FromProto(
    const proto::v1::ResourceSet& proto_resources,
    ResourceSet* out
);

}  // namespace dar::serialization