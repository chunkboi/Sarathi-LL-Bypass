#pragma once

#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string_view>

namespace seb::num {

// Strict locale-independent floating-point parser via C++17/26 std::from_chars.
// The caller supplies a single, already-trimmed token; requires full
// consumption and rejects infinities/NaN.
[[nodiscard]] inline double parse_double_strict(std::string_view token) {
    if (token.empty()) {
        throw std::runtime_error("invalid floating-point number");
    }

    double value = 0.0;
    const char* const begin = token.data();
    const char* const end = begin + token.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end || !std::isfinite(value)) {
        throw std::runtime_error("invalid or out-of-range floating-point number");
    }
    return value;
}

} // namespace seb::num
