#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

namespace minidb {

// Failure while tokenizing or parsing. `line` and `column` are 1-based.
class ParseError : public std::runtime_error {
  public:
    ParseError(const std::string& message, std::size_t line, std::size_t column)
        : std::runtime_error(message), line_(line), column_(column) {}

    std::size_t line() const noexcept { return line_; }
    std::size_t column() const noexcept { return column_; }

  private:
    std::size_t line_;
    std::size_t column_;
};

}  // namespace minidb
