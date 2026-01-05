#pragma once
#include <string>
#include <optional>

namespace Hyperion::Core {
    class InputIngest {
    public:
        // Returns the new content if changed, otherwise nullopt
        static std::optional<std::string> check();
    };
}
