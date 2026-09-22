#pragma once

#include <cstddef>
#include <string>

namespace minidb {

enum class TokenKind {
    EndOfInput,
    Identifier,
    IntegerLiteral,
    FloatLiteral,
    StringLiteral,

    KwCreate,
    KwTable,
    KwDrop,
    KwInsert,
    KwInto,
    KwValues,
    KwSelect,
    KwFrom,
    KwWhere,
    KwUpdate,
    KwSet,
    KwDelete,
    KwInt,
    KwText,
    KwBoolean,
    KwFloat,
    KwTrue,
    KwFalse,

    Star,
    Comma,
    LeftParen,
    RightParen,
    Semicolon,

    Equal,
    NotEqual,
    Less,
    Greater,
    LessEqual,
    GreaterEqual,
};

// One lexeme. `line` and `column` are 1-based and point at the first character.
// `text` is the source spelling, except for string literals, where it is the
// decoded value (`''` inside quotes becomes a single quote).
struct Token {
    TokenKind kind = TokenKind::EndOfInput;
    std::string text;
    std::size_t line = 1;
    std::size_t column = 1;
};

}  // namespace minidb
