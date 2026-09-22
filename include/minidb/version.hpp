#pragma once

#include <string_view>

namespace minidb {

// User-facing release printed by the CLI. M0 is the skeleton, version 0.1.
inline constexpr std::string_view kVersion = "0.1";

}  // namespace minidb
