#pragma once

#include <optional>

namespace Startup
{
    // nullopt means Windows denied the read or the registration is malformed.
    std::optional<bool> IsEnabled();
    bool SetEnabled(bool enabled);
}
