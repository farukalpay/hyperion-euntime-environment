#include "../../include/core/InputIngest.hpp"
#include <cstdio>
#include <array>
#include <memory>
#include <chrono>
#include <iostream>

namespace Hyperion::Core {

    static std::string s_last_clip = "";
    static auto s_last_check = std::chrono::steady_clock::now();

    std::optional<std::string> InputIngest::check() {
        // Throttle to 200ms to avoid burning CPU with popen
        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - s_last_check).count() < 200) {
            return std::nullopt;
        }
        s_last_check = now;

        // Use pbpaste (Mac standard)
        std::array<char, 128> buffer;
        std::string result;
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen("pbpaste", "r"), pclose);
        if (!pipe) {
            return std::nullopt;
        }
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            result += buffer.data();
        }

        // Detect change
        if (!result.empty() && result != s_last_clip) {
            s_last_clip = result;
            return result;
        }
        return std::nullopt;
    }
}
