#pragma once

#include <stdexcept>
#include <string>

namespace minidb {

// A user mistake while executing a parsed statement: missing table, wrong
// type, duplicate name, unknown column, bad INSERT arity, and similar.
// These are not parse errors and they must not abort the process.
class ExecutionError : public std::runtime_error {
  public:
    explicit ExecutionError(const std::string& message) : std::runtime_error(message) {}
};

}  // namespace minidb
