#include "dar/core/resource.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace dar
{
    namespace
    {
        [[nodiscard]] bool IsValidResourceName(std::string_view name) noexcept
        {
            if(name.empty())
            {
                return false;
            }

            /**
             * name for resource: CPU, GPU, memory, node-resource,...
             */
            for(const char c:name)
            {
                const bool is_lower = c>='a'&& c<='z';
                const bool is_upper = c>='A'&&c<='Z';
                const bool is_digit = c>='0'&& c<='9';
                const bool is_separator = c=='_'||c=='-'||c=='.';
                // If it is not any of them 
                // Normal: 3 false, 1 true. (1 false, 3 true) -> not enter
                // If none: 4 false (4 true)->enters
                if(!is_lower&&!is_upper&&!is_digit&&!is_separator)
                {
                    return false;
                }
            }
            return true;
        }
    }

    // Potential two edge cases
    /**
     * 1. std::llround Overflow: if scaled exceeds maximum value an int64_t can hold, call std::llround can cause undefined behavior
     * 2. -0.0<0.0 evaluate to false?
     */
    // Convert a double into a fixed-point integer (int64_t)
    std::optional<ResourceQuantity> ResourceQuantity::FromDouble(double value) noexcept
    {
        //Resource quantities must be finite and non-negative
        if(!std::isfinite(value)||value<0.0)
        {
            return std::nullopt;
        }

        // 1 -> 10000(double)
        const double scaled = value * static_cast<double>(kScale);
        
        //constexpr double kMaxScaled = 9223372036854774784.0; // Largest double <= INT64_MAX
        
        // If exceed the largest value that int64_t can hold (safety check)
        // Prevent overflowing
        if(scaled > static_cast<double>(std::numeric_limits<std::int64_t>::max()))
        {
            return std::nullopt;
        }

        // Round to the nearest 0.0001 logical resource unit
        const auto internal_value = static_cast<std::int64_t>(std::llround(scaled));

        return ResourceQuantity(internal_value);
    }

    // Ex: From 10000 -> 1(double)
    double ResourceQuantity::ToDouble() const noexcept
    {
        return static_cast<double>(value_)/static_cast<double>(kScale);
    }

    Status ResourceSet::Set(std::string name, double quantity)
    {
        if(!IsValidResourceName(name))
        {
            return Status::InvalidArgument("invalid resource name");
        }

        // Ex: 1->10000(double)
        const auto parsed = ResourceQuantity::FromDouble(quantity);
        if(!parsed.has_value())
        {
            // Triggered by edge cases in FromDouble
            return Status::InvalidArgument("Invalid Resource Quantity");
        }

        return Set(std::move(name),*parsed);
    }

    Status ResourceSet::Set(std::string name, ResourceQuantity quantity)
    {
        if(!IsValidResourceName(name))
        {
            return Status::InvalidArgument("Invalid Resource Name");
        }
        
        // Use the < defined in ResourceQuantity
        // Where the "friend" is being useful
        // if smaller than minimum defined value
        if(quantity<ResourceQuantity{})
        {
            return Status::InvalidArgument("resource quantity cannot be negative");
        }

        // Zero quantity: Absence from map
        if(quantity.IsZero())
        {
            values_.erase(name);
            return Status::OK();
        }
        values_.insert_or_assign(std::move(name),quantity);

        return Status::OK();
    }

    ResourceQuantity ResourceSet::Get(std::string_view name) const noexcept 
    {
        const auto it = values_.find(name);

        // if not found
        if(it == values_.end())
        {
            return ResourceQuantity{};
        }

        // return the value of key/value
        return it->second;
    }

    bool ResourcePool::CanFit(const ResourceRequest& request) const noexcept
    {
        for(const auto& [name,requested]:request.resources().values())
        {
            const ResourceQuantity available = available_.Get(name);
            // use > defined in ResourceQuantity
            // The comparison is for all resource requested(loop through it, if one not met then false)
            if(requested > available)
            {
                return false;
            }
        }
        return true;
    }

    Status ResourcePool::Acquire(const ResourceRequest& request)
    {
        // validate the entire request before changing availability
        if(!CanFit(request))
        {
            
            return Status::ResourceExhausted("Requestsed resources do not fit current availability");
        }

        for(const auto& [name,requested]:request.resources().values())
        {
            // Use the - defined in ResourceQuantity(loop through eaech term)
            const ResourceQuantity remaining = available_.Get(name)-requested;

            // Now set the value of resource available after occupied by current usage
            const Status status = available_.Set(name, remaining);

            // Separation of user error and system error
            // CanFit() remaining >=0, so if failed here that means internal accounting invariant has been violated
            //ok() to check if the status == ok
            if(!status.ok())
            {
                return Status::Internal("Resource Invariant violated during acquire");
            }
        }
        
        // Ok() set status to default state
        return Status::OK();
    }

    Status ResourcePool::Release(const ResourceRequest& request)
    {
        // First validate every resource without changing anything
        // Prevent parially completed release if one entry in request is invalid
        for(const auto& [name, released]:request.resources().values())
        {
            const ResourceQuantity total = total_.Get(name);
            const ResourceQuantity available = available_.Get(name);
            // use the - defined in ResourceQuantity
            const ResourceQuantity in_use = total - available;

            // > defined in ResourceQuantity
            // Trying to release more than it is in-use (error)
            if(released>in_use)
            {
                return Status::FailedPrecondition("release exceeds acquired amount for resource");
            }
        }

        // validation succeeded for every resource, so mutation can occur
        for(const auto& [name,released]:request.resources().values())
        {
            const ResourceQuantity next = available_.Get(name)+released;
            const Status status = available_.Set(name, next);
            if(!status.ok())
            {
                return Status::Internal("Resource Invariant violated during release");
            }
        }
        return Status::OK();
    }
}