#include "dar/serialization/serialization.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace dar::serialization
{
namespace
{

template <typename Id, typename ProtoId>
void IDToProto(const Id& id, ProtoId* out)
{
    if (out == nullptr)
    {
        return;
    }

    const auto& bytes = id.bytes();

    out->set_value(
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()
    );
}

template <typename Id, typename ProtoId>
Status IDFromProto(const ProtoId& proto_id, Id* out)
{
    if (out == nullptr)
    {
        return Status::InvalidArgument(
            "output ID pointer must not be null"
        );
    }

    const std::string& wire_bytes = proto_id.value();

    if (wire_bytes.size() != Id::kSize)
    {
        return Status::InvalidArgument(
            "wire ID must contain exactly 16 bytes"
        );
    }

    // StrongID currently intentionally exposes FromHex(), but not
    // FromBytes(). Convert the 16 wire bytes into the existing
    // 32-character hexadecimal representation and reuse FromHex().
    static constexpr char kHexDigits[] = "0123456789abcdef";

    std::string hex;
    hex.resize(Id::kSize * 2);

    for (std::size_t i = 0; i < Id::kSize; ++i)
    {
        const auto byte =
            static_cast<std::uint8_t>(
                static_cast<unsigned char>(wire_bytes[i])
            );

        hex[2 * i] = kHexDigits[(byte >> 4U) & 0x0FU];
        hex[2 * i + 1] = kHexDigits[byte & 0x0FU];
    }

    auto parsed = Id::FromHex(hex);

    if (!parsed.has_value())
    {
        return Status::InvalidArgument(
            "wire ID could not be decoded"
        );
    }

    *out = *parsed;
    return Status::OK();
}

proto::v1::StatusCode StatusCodeToProto(StatusCode code)
{
    switch (code)
    {
        case StatusCode::kOk:
            return proto::v1::STATUS_CODE_OK;

        case StatusCode::kInvalidArgument:
            return proto::v1::STATUS_CODE_INVALID_ARGUMENT;

        case StatusCode::KNotFound:
            return proto::v1::STATUS_CODE_NOT_FOUND;

        case StatusCode::KAlreadyExists:
            return proto::v1::STATUS_CODE_ALREADY_EXISTS;

        case StatusCode::KFailedPrecondition:
            return proto::v1::STATUS_CODE_FAILED_PRECONDITION;

        case StatusCode::KUnavailable:
            return proto::v1::STATUS_CODE_UNAVAILABLE;

        case StatusCode::KResourceExhausted:
            return proto::v1::STATUS_CODE_RESOURCE_EXHAUSTED;

        case StatusCode::Kcancelled:
            return proto::v1::STATUS_CODE_CANCELLED;

        case StatusCode::KDeadlineExceeded:
            return proto::v1::STATUS_CODE_DEADLINE_EXCEEDED;

        case StatusCode::KInternal:
            return proto::v1::STATUS_CODE_INTERNAL;
    }

    return proto::v1::STATUS_CODE_INTERNAL;
}

}  // namespace

// ============================================================
// Strong IDs
// ============================================================

void ToProto(const AgentID& id, proto::v1::AgentId* out)
{
    IDToProto(id, out);
}

void ToProto(const TaskID& id, proto::v1::TaskId* out)
{
    IDToProto(id, out);
}

void ToProto(const ExecutionID& id, proto::v1::ExecutionId* out)
{
    IDToProto(id, out);
}

void ToProto(const ObjectID& id, proto::v1::ObjectId* out)
{
    IDToProto(id, out);
}

void ToProto(const WorkerID& id, proto::v1::WorkerId* out)
{
    IDToProto(id, out);
}

void ToProto(const NodeID& id, proto::v1::NodeId* out)
{
    IDToProto(id, out);
}

void ToProto(const TenantID& id, proto::v1::TenantId* out)
{
    IDToProto(id, out);
}

void ToProto(const DeploymentID& id, proto::v1::DeploymentId* out)
{
    IDToProto(id, out);
}

Status FromProto(const proto::v1::AgentId& id, AgentID* out)
{
    return IDFromProto(id, out);
}

Status FromProto(const proto::v1::TaskId& id, TaskID* out)
{
    return IDFromProto(id, out);
}

Status FromProto(const proto::v1::ExecutionId& id, ExecutionID* out)
{
    return IDFromProto(id, out);
}

Status FromProto(const proto::v1::ObjectId& id, ObjectID* out)
{
    return IDFromProto(id, out);
}

Status FromProto(const proto::v1::WorkerId& id, WorkerID* out)
{
    return IDFromProto(id, out);
}

Status FromProto(const proto::v1::NodeId& id, NodeID* out)
{
    return IDFromProto(id, out);
}

Status FromProto(const proto::v1::TenantId& id, TenantID* out)
{
    return IDFromProto(id, out);
}

Status FromProto(const proto::v1::DeploymentId& id, DeploymentID* out)
{
    return IDFromProto(id, out);
}

// ============================================================
// Status
// ============================================================

void ToProto(const Status& status, proto::v1::Status* out)
{
    if (out == nullptr)
    {
        return;
    }

    out->set_code(StatusCodeToProto(status.code()));
    out->set_message(std::string(status.message()));
}

Status FromProtoStatus(const proto::v1::Status& status)
{
    switch (status.code())
    {
        case proto::v1::STATUS_CODE_OK:
            return Status::OK();

        case proto::v1::STATUS_CODE_INVALID_ARGUMENT:
            return Status::InvalidArgument(status.message());

        case proto::v1::STATUS_CODE_NOT_FOUND:
            return Status::NotFound(status.message());

        case proto::v1::STATUS_CODE_ALREADY_EXISTS:
            return Status::AlreadyExists(status.message());

        case proto::v1::STATUS_CODE_FAILED_PRECONDITION:
            return Status::FailedPrecondition(status.message());

        case proto::v1::STATUS_CODE_UNAVAILABLE:
            return Status::Unavailable(status.message());

        case proto::v1::STATUS_CODE_RESOURCE_EXHAUSTED:
            return Status::ResourceExhausted(status.message());

        case proto::v1::STATUS_CODE_CANCELLED:
            return Status::Cancelled(status.message());

        case proto::v1::STATUS_CODE_DEADLINE_EXCEEDED:
            return Status::DeadlineExceeded(status.message());

        case proto::v1::STATUS_CODE_INTERNAL:
            return Status::Internal(status.message());

        case proto::v1::STATUS_CODE_UNSPECIFIED:
            return Status::Internal(
                "received unspecified runtime status code"
            );
    }

    return Status::Internal(
        "received unknown runtime status code"
    );
}

// ============================================================
// Resources
// ============================================================

void ToProto(
    const ResourceSet& resources,
    proto::v1::ResourceSet* out)
{
    if (out == nullptr)
    {
        return;
    }

    out->clear_quantities();

    auto* quantities = out->mutable_quantities();

    for (const auto& [name, quantity] : resources.values())
    {
        (*quantities)[name] = quantity.ToDouble();
    }
}

Status FromProto(
    const proto::v1::ResourceSet& proto_resources,
    ResourceSet* out)
{
    if (out == nullptr)
    {
        return Status::InvalidArgument(
            "output ResourceSet pointer must not be null"
        );
    }

    ResourceSet decoded;

    for (const auto& [name, quantity] :
         proto_resources.quantities())
    {
        const Status status = decoded.Set(name, quantity);

        if (!status.ok())
        {
            return Status::InvalidArgument(
                "invalid wire resource '" +
                name +
                "': " +
                status.ToString()
            );
        }
    }

    *out = std::move(decoded);
    return Status::OK();
}

}  // namespace dar::serialization
