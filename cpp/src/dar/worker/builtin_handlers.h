#pragma once

#include "dar/common/status.h"
#include "dar/worker/handler_registry.h"

namespace dar
{

// Register the minimal built-in handlers used to prove the
// Phase-3 distributed execution boundary.
Status RegisterBuiltinHandlers(HandlerRegistry& registry);

}  // namespace dar