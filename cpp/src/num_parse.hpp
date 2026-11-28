#pragma once

#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>

namespace seb::num {

// Portable strict floating-point parser.
//
// libstdc++ 11 only provides the integer std::from_chars overloads, so use
// strtod for the double path. The caller supplies a single, already-trimmed
// token; require full consumption and reject infinities/NaN.
[[nodiscard]] inline double parse_double_strict(std::string_view token) {
    if (token.empty()) {
        throw std::runtime_error("invalid floating-point number");
    }

    std::string text(token);
    const char* begin = text.c_str();
    char* end = nullptr;
    const double value = std::strtod(begin, &end);
    if (end != begin + text.size() || !std::isfinite(value)) {
        throw std::runtime_error("invalid or out-of-range floating-point number");
    }
    return value;
}

} // namespace seb::num
