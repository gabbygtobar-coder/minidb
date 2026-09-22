#pragma once

#include <string_view>

namespace minidb {

// User-facing release printed by the CLI. Milestone scope is tracked in the README.
inline constexpr std::string_view kVersion = "0.1";

}  // namespace minidb
