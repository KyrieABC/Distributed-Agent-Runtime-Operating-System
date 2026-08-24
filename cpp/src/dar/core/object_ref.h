#pragma once

#include "dar/common/id.h"
#include "dar/common/status.h"

namespace dar
{
    // A lightweight logical refernce to an object

    // Only object identity, Object ownership, location, storage, fetching, lifetime, and distributed reference
    class ObjectRef final
    {
    public:
        ObjectRef() = default;

        explicit ObjectRef(ObjectID id)  : id_(id){}

        [[nodiscard]] const ObjectID& id() const noexcept
        {
            return id_;
        }

        [[nodiscard]] bool valid() const noexcept
        {
            return !id_.IsNil();
        }

        [[nodiscard]] Status Validate() const;
    private:
        ObjectID id_;
    };
}