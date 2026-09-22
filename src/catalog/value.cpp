#include "minidb/value.hpp"

#include "minidb/execution_error.hpp"

#include <charconv>
#include <stdexcept>
#include <system_error>

namespace minidb {
namespace {

const char* literal_type_name(LiteralKind kind) {
    switch (kind) {
        case LiteralKind::Integer:
            return "INT";
        case LiteralKind::Float:
            return "FLOAT";
        case LiteralKind::Boolean:
            return "BOOLEAN";
        case LiteralKind::String:
            return "TEXT";
    }
    throw std::logic_error("unknown literal kind");
}

std::string format_float(double value) {
    char buffer[64];
    const std::to_chars_result result =
        std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general);
    if (result.ec != std::errc()) {
        throw std::logic_error("failed to format floating-point value");
    }
    std::string text(buffer, result.ptr);
    if (text.find('.') == std::string::npos && text.find('e') == std::string::npos &&
        text.find('E') == std::string::npos) {
        text += ".0";
    }
    return text;
}

}  // namespace

const char* data_type_name(DataType type) {
    switch (type) {
        case DataType::Int:
            return "INT";
        case DataType::Text:
            return "TEXT";
        case DataType::Boolean:
            return "BOOLEAN";
        case DataType::Float:
            return "FLOAT";
    }
    throw std::logic_error("unknown data type");
}

Value::Value(DataType type, std::int64_t integer, double floating, bool boolean, std::string text)
    : type_(type),
      integer_(integer),
      floating_(floating),
      boolean_(boolean),
      text_(std::move(text)) {}

Value Value::integer(std::int64_t value) {
    return Value(DataType::Int, value, 0.0, false, {});
}

Value Value::floating(double value) {
    return Value(DataType::Float, 0, value, false, {});
}

Value Value::boolean(bool value) {
    return Value(DataType::Boolean, 0, 0.0, value, {});
}

Value Value::text(std::string value) {
    return Value(DataType::Text, 0, 0.0, false, std::move(value));
}

std::int64_t Value::integer() const {
    if (type_ != DataType::Int) {
        throw std::logic_error("Value::integer called on a non-INT value");
    }
    return integer_;
}

double Value::floating() const {
    if (type_ != DataType::Float) {
        throw std::logic_error("Value::floating called on a non-FLOAT value");
    }
    return floating_;
}

bool Value::boolean() const {
    if (type_ != DataType::Boolean) {
        throw std::logic_error("Value::boolean called on a non-BOOLEAN value");
    }
    return boolean_;
}

const std::string& Value::text() const {
    if (type_ != DataType::Text) {
        throw std::logic_error("Value::text called on a non-TEXT value");
    }
    return text_;
}

std::string Value::format() const {
    switch (type_) {
        case DataType::Int:
            return std::to_string(integer_);
        case DataType::Float:
            return format_float(floating_);
        case DataType::Boolean:
            return boolean_ ? "TRUE" : "FALSE";
        case DataType::Text:
            return text_;
    }
    throw std::logic_error("unknown data type");
}

Value value_from_literal(const Literal& literal, DataType expected, const std::string& column) {
    const bool matches = (expected == DataType::Int && literal.kind == LiteralKind::Integer) ||
                         (expected == DataType::Float && literal.kind == LiteralKind::Float) ||
                         (expected == DataType::Text && literal.kind == LiteralKind::String) ||
                         (expected == DataType::Boolean && literal.kind == LiteralKind::Boolean);
    if (!matches) {
        throw ExecutionError("Type mismatch for column " + column + ": expected " +
                             data_type_name(expected) + ", got " + literal_type_name(literal.kind));
    }
    switch (literal.kind) {
        case LiteralKind::Integer:
            return Value::integer(literal.integer);
        case LiteralKind::Float:
            return Value::floating(literal.floating);
        case LiteralKind::Boolean:
            return Value::boolean(literal.boolean);
        case LiteralKind::String:
            return Value::text(literal.text);
    }
    throw std::logic_error("unknown literal kind");
}

}  // namespace minidb
