#pragma once

#include "minidb/ast.hpp"
#include "minidb/parse_error.hpp"

#include <string_view>

namespace minidb {

// Parses exactly one statement from `source`. A single trailing semicolon is
// optional. Leading and trailing whitespace is ignored. Throws ParseError when
// the input is not one statement in the MiniDB subset.
//
// The parser does not execute the statement and does not touch storage.
Statement parse_statement(std::string_view source);

}  // namespace minidb
