#pragma once

#include "minidb/ast.hpp"

#include <cstdint>
#include <string>

namespace minidb {

// One stored cell. The type is fixed when the value is created and matches
// the column that holds it. There is no NULL in this subset.
class Value {
  public:
    static Value integer(std::int64_t value);
    static Value floating(double value);
    static Value boolean(bool value);
    static Value text(std::string value);

    DataType type() const noexcept { return type_; }

    std::int64_t integer() const;
    double floating() const;
    bool boolean() const;
    const std::string& text() const;

    // Display form used in SELECT output: decimal integers, TRUE/FALSE,
    // the raw string, and a shortest round-trip float (with ".0" when the
    // shortest form would otherwise look like an integer).
    std::string format() const;

  private:
    Value(DataType type, std::int64_t integer, double floating, bool boolean, std::string text);

    DataType type_;
    std::int64_t integer_ = 0;
    double floating_ = 0.0;
    bool boolean_ = false;
    std::string text_;
};

const char* data_type_name(DataType type);

// Builds a cell for `expected`. Throws ExecutionError when the literal kind
// does not match that column type. Integer literals are not accepted as FLOAT.
Value value_from_literal(const Literal& literal, DataType expected, const std::string& column);

}  // namespace minidb
