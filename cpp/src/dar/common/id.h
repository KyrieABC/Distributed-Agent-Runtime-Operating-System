// This vs ifndef
# pragma once

// [[nodiscard]] is a C++ attribute in C++17, tells compiler to warn user if the return value of a function or class is ignored
// noexcept: 1. Decalre function that will not throw exception 2. Check at compile time if an expression can throw
// friend grants external function or classes direct access to private and protected member of class
// using: Create type alias (IdBytes(template aliases for array<>))

#include <array>
// Library header the provide fundamental, type-safe definitions inherited from C's <stddef.h>
#include <cstddef>
// A set of fixed-width integer types and limits within the std namespace
#include <cstdint>
// type-safe wrapper template that manages a value that may or may not exist
#include <optional>
#include <ostream>
#include <string>
// Lightweight, non-owning, read-only wrapper around an existing sequence of characters
#include <string_view>

// nested namespace, detail placed inside dar
namespace dar::detail
{
    // Use std::array<std::uint8_t,16> instead of std::string to keep 128-bit IDs allocated contiguously on stack without heap allocation overhead
    // 128-bit raw byte storage 
    using IdBytes = std::array<std::uint8_t,16>;
    // Generate 16 random bytes for an ID
    [[nodiscard]] IdBytes GenerateRandomIDBytes();
    // Convers a 32-character hex string into 16 raw bytes; return null on invalid input
    [[nodiscard]] std::optional<IdBytes> ParseHexID(std::string_view hex) noexcept;
    // Serialize 16 raw bytes into a lowercase 32 character hex string
    [[nodiscard]] std::string HexID(const IdBytes& bytes);
}

namespace dar
{
    // StrongID follows the same high-level idea as Ray's typed ID classes:
    // identities have a fixed binary representation
    // C++ type system prevents accidentally passing a TaskID where an AgentID is required
    /**
     * Even tho Tag is not used as type in class,
     * But: StrongID<A> is treated as different class from StrongID<B>
     * are treated as different class-types
     */
    template <typename Tag>
    class StrongID final
    {
        public:
            static constexpr std::size_t kSize = 16;
            // Compiler generate a standard, optimized implementation
            StrongID() = default;
            [[nodiscard]] static StrongID Nil() noexcept
            {
                return {};
            }
            [[nodiscard]] static StrongID Random()
            {
                return StrongID(detail::GenerateRandomIDBytes());
            }
            // from hexidecimal to StrongID object (return StrongID(*bytes))
            [[nodiscard]] static std::optional<StrongID> FromHex(std::string_view hex) noexcept
            {
                auto bytes = detail::ParseHexID(hex);
                if(!bytes.has_value())
                {
                    return std::nullopt;
                }
                // Does *bytes go from Hex -> StrongID
                return StrongID(*bytes);
            }
            [[nodiscard]] bool IsNil() const noexcept
            {
                for(const auto byte:bytes_)
                {
                    if(byte!=0U)
                    {
                        return false;
                    }
                }
                return true;
            }
            [[nodiscard]] std::string Hex() const
            {
                return detail::HexID(bytes_);
            }
            [[nodiscard]] const detail::IdBytes& bytes() const noexcept
            {
                return bytes_;
            }
            friend bool operator==(const StrongID& lhs,const StrongID& rhs)
            {
                return lhs.bytes_==rhs.bytes_;
            }
            friend bool operator!=(const StrongID& lhs,const StrongID& rhs)
            {
                return !(lhs.bytes_==rhs.bytes_);
            }
            friend bool operator<(const StrongID& lhs, const StrongID& rhs) noexcept
            {
                return lhs.bytes_ < rhs.bytes_;
            }
        private:
            explicit StrongID(detail::IdBytes bytes):bytes_(bytes){}
            // IdBytes: std::array<std::uint8_t,16>, value initializating a std::array of built-in integer types zero-initializes every element
            detail::IdBytes bytes_{};
    };


    template <typename Tag>
    std::ostream& operator<<(std::ostream& os, const StrongID<Tag>& id)
    {
        return os << id.Hex();
    }

    struct AgentIdTag;
    struct TaskIdTag;
    struct ExecutionIdTag;
    struct ObjectIdTag;
    struct WorkerIdTag;
    struct NodeIdTag;
    struct TenantIdTag;
    struct DeploymentIdTag;

    // where the typename for StrongID is applied
    using AgentID = StrongID<AgentIdTag>;
    using TaskID = StrongID<TaskIdTag>;
    using ExecutionID = StrongID<ExecutionIdTag>;
    using ObjectID = StrongID<ObjectIdTag>;
    using WorkerID = StrongID<WorkerIdTag>;
    using NodeID = StrongID<NodeIdTag>;
    using TenantID = StrongID<TenantIdTag>;
    using DeploymentID = StrongID<DeploymentIdTag>;

    template <typename Id>
    struct StrongIDHash final
    {
        std::size_t operator()(const Id& id) const noexcept
        {
            //FNV-1a is only used for unordered container bucket placement
            //It is not used as an identity generator or security primitive
            std::size_t hash = static_cast<std::size_t>(1469598103934665603ULL);
            for(const auto byte:id.bytes())
            {
                hash ^= static_cast<std::size_t>(byte);
                hash *= static_cast<std::size_t>(1099511628211ULL);
            }
            return hash;
        }

    };

}

