#include "dar/core/object_ref.h"

namespace dar
{
    Status ObjectRef::Validate() const
    {
        if(!valid())
        {
            return Status::InvalidArgument("object reference id cannot be nil");
        }
         return Status::OK();
    }
}