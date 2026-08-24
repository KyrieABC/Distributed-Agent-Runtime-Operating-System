/**
 * Key API
 * class ResourceQuantity
 * class ResourceSet
 * class ResourceRequest
 * class ResourcePool
 */

#pragma once

// fixed-width, minimum-width, fastest-minimum-width integer types
#include <cstdint>
// class templates and utilities designed to handle callable objects and function objects
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "dar/common/status.h"

namespace dar
{
    class ResourceQuantity final
    {
    public:
        // 1.0 logical resource unit == 10000 internal units
        // Fixed-point representation avoids floating-point arithmetic inside
        // scheduler accounting while preserving 0.0001 unit granularity
        static constexpr std::int64_t kScale = 10000;

        ResourceQuantity() = default;

        [[nodiscard]] static std::optional<ResourceQuantity> FromDouble(double value) noexcept;

        [[nodiscard]] double ToDouble() const noexcept;

        [[nodiscard]] bool IsZero() const noexcept
        {
            return value_== 0;
        }

        friend bool operator==(const ResourceQuantity& lhs, const ResourceQuantity& rhs)
        {
            return lhs.value_==rhs.value_;
        }

        friend bool operator!=(const ResourceQuantity& lhs, const ResourceQuantity& rhs)
        {
            return !(lhs.value_==rhs.value_);
        }

        friend bool operator<(const ResourceQuantity& lhs, const ResourceQuantity& rhs)
        {
            return lhs.value_<rhs.value_;
        }
    
        friend bool operator<=(const ResourceQuantity& lhs, const ResourceQuantity& rhs)
        {
            return lhs.value_<=rhs.value_;
        }

        friend bool operator>(const ResourceQuantity& lhs, const ResourceQuantity& rhs)
        {
            return lhs.value_>rhs.value_;
        }

        friend bool operator>=(const ResourceQuantity& lhs, const ResourceQuantity& rhs)
        {
            return lhs.value_>=rhs.value_;
        }

        friend ResourceQuantity operator+(const ResourceQuantity& lhs, const ResourceQuantity& rhs)
        {
            return ResourceQuantity(lhs.value_+rhs.value_);
        }

        friend ResourceQuantity operator-(const ResourceQuantity& lhs, const ResourceQuantity& rhs)
        {
            return ResourceQuantity(lhs.value_-rhs.value_);
        }
    private:
        explicit ResourceQuantity(std::int64_t value) noexcept 
          : value_(value){}
        std::int64_t value_{0};
    };

    class ResourceSet final
    {
    public:
        // std::less -> performs a less-than comparison between no values
        // std::map use std::less by default as third template argument to sort its keys
        using Map = std::map<std::string,ResourceQuantity,std::less<>>;
        ResourceSet() = default;

        // Adds or updates a logical resource
        // Quantity == 0 removes the resource
        // Negative, non-finite, or otherwise invalid quantities are rejected
        Status Set(std::string name, double quantity);

        //Internal fixed-point overload used by resource accounting
        Status Set(std::string name, ResourceQuantity quantity);

        // Missing resources are treated as 0
        [[nodiscard]] ResourceQuantity Get(std::string_view name) const noexcept;

        friend bool operator==(const ResourceSet& lhs, const ResourceSet& rhs) noexcept
        {
            return lhs.values_==rhs.values_;
        }

        friend bool operator!=(const ResourceSet& lhs, const ResourceSet& rhs) noexcept
        {
            return !(lhs.values_==rhs.values_);
        }

        [[nodiscard]] const Map& values() const noexcept
        {
            return values_;
        }
    private:
        Map values_;
    };

    class ResourceRequest final
    {
    public:
        ResourceRequest() = default;

        explicit ResourceRequest(ResourceSet resources) : resources_(std::move(resources)) {}
        
        [[nodiscard]] const ResourceSet& resources() const noexcept
        {
            return resources_;
        }

    private:
        ResourceSet resources_;
    };

    class ResourcePool final
    {
    public:
        explicit ResourcePool(ResourceSet total) : total_(std::move(total)), available_(total_) {}
        // Return true if every requested resource can be satisfied by currently available logical resource
        [[nodiscard]] bool CanFit(const ResourceRequest& request) const noexcept;

        // Acquire and Release are atomic (cannot be interrupted) across complete request:
        // validation occurs before any resource quantity is modified
        Status Acquire(const ResourceRequest& request);
        Status Release(const ResourceRequest& request);

        [[nodiscard]] const ResourceSet& Total() const noexcept
        {
            return total_;
        }

        [[nodiscard]] const ResourceSet& Available() const noexcept
        {
            return available_;
        }

    private:
        ResourceSet total_;
        ResourceSet available_;
    };
}