#include "minidb/lexer.hpp"

#include <cstdio>
#include <string>
#include <utility>

namespace minidb {
namespace {

bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

bool is_ident_start(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

bool is_ident_continue(char c) {
    return is_ident_start(c) || is_digit(c);
}

char ascii_lower(char c) {
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c - 'A' + 'a');
    }
    return c;
}

bool equals_keyword(std::string_view word, std::string_view keyword) {
    if (word.size() != keyword.size()) {
        return false;
    }
    for (std::size_t i = 0; i < word.size(); ++i) {
        if (ascii_lower(word[i]) != keyword[i]) {
            return false;
        }
    }
    return true;
}

TokenKind keyword_kind(std::string_view word) {
    struct Entry {
        std::string_view text;
        TokenKind kind;
    };
    static constexpr Entry kKeywords[] = {
        {"boolean", TokenKind::KwBoolean},
        {"create", TokenKind::KwCreate},
        {"delete", TokenKind::KwDelete},
        {"drop", TokenKind::KwDrop},
        {"false", TokenKind::KwFalse},
        {"float", TokenKind::KwFloat},
        {"from", TokenKind::KwFrom},
        {"index", TokenKind::KwIndex},
        {"insert", TokenKind::KwInsert},
        {"int", TokenKind::KwInt},
        {"into", TokenKind::KwInto},
        {"on", TokenKind::KwOn},
        {"select", TokenKind::KwSelect},
        {"set", TokenKind::KwSet},
        {"table", TokenKind::KwTable},
        {"text", TokenKind::KwText},
        {"true", TokenKind::KwTrue},
        {"update", TokenKind::KwUpdate},
        {"values", TokenKind::KwValues},
        {"where", TokenKind::KwWhere},
    };
    for (const Entry& entry : kKeywords) {
        if (equals_keyword(word, entry.text)) {
            return entry.kind;
        }
    }
    return TokenKind::Identifier;
}

std::string unexpected_character(char c) {
    const auto uc = static_cast<unsigned char>(c);
    if (uc >= 32 && uc < 127 && c != '\'' && c != '\\') {
        return std::string("unexpected character '") + c + "'";
    }
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "unexpected character '\\x%02x'", uc);
    return buffer;
}

}  // namespace

Lexer::Lexer(std::string_view source) : source_(source) {}

bool Lexer::at_end() const {
    return pos_ >= source_.size();
}

char Lexer::peek(std::size_t ahead) const {
    if (pos_ + ahead >= source_.size()) {
        return '\0';
    }
    return source_[pos_ + ahead];
}

void Lexer::advance() {
    if (at_end()) {
        return;
    }
    if (source_[pos_] == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    ++pos_;
}

void Lexer::skip_whitespace() {
    while (!at_end()) {
        const char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f') {
            advance();
            continue;
        }
        break;
    }
}

Token Lexer::take(TokenKind kind, std::size_t length) {
    const std::size_t start_line = line_;
    const std::size_t start_column = column_;
    const std::size_t start = pos_;
    for (std::size_t i = 0; i < length; ++i) {
        advance();
    }
    return Token{kind, std::string(source_.substr(start, length)), start_line, start_column};
}

Token Lexer::identifier() {
    const std::size_t start_line = line_;
    const std::size_t start_column = column_;
    const std::size_t start = pos_;
    advance();
    while (is_ident_continue(peek())) {
        advance();
    }
    std::string text(source_.substr(start, pos_ - start));
    const TokenKind kind = keyword_kind(text);
    return Token{kind, std::move(text), start_line, start_column};
}

Token Lexer::number() {
    const std::size_t start_line = line_;
    const std::size_t start_column = column_;
    const std::size_t start = pos_;
    bool is_float = false;

    if (peek() == '.') {
        is_float = true;
        advance();
        while (is_digit(peek())) {
            advance();
        }
    } else {
        while (is_digit(peek())) {
            advance();
        }
        if (peek() == '.') {
            is_float = true;
            advance();
            while (is_digit(peek())) {
                advance();
            }
        }
    }

    std::string text(source_.substr(start, pos_ - start));
    const TokenKind kind = is_float ? TokenKind::FloatLiteral : TokenKind::IntegerLiteral;
    return Token{kind, std::move(text), start_line, start_column};
}

Token Lexer::string_literal() {
    const std::size_t start_line = line_;
    const std::size_t start_column = column_;
    advance();  // opening quote
    std::string value;
    while (!at_end()) {
        const char c = peek();
        if (c == '\'') {
            advance();
            if (!at_end() && peek() == '\'') {
                advance();
                value.push_back('\'');
                continue;
            }
            return Token{TokenKind::StringLiteral, std::move(value), start_line, start_column};
        }
        advance();
        value.push_back(c);
    }
    throw ParseError("unterminated string literal", start_line, start_column);
}

Token Lexer::next() {
    skip_whitespace();
    if (at_end()) {
        return Token{TokenKind::EndOfInput, "", line_, column_};
    }

    const char c = peek();
    if (is_ident_start(c)) {
        return identifier();
    }
    if (is_digit(c) || (c == '.' && is_digit(peek(1)))) {
        return number();
    }

    switch (c) {
        case '\'':
            return string_literal();
        case '*':
            return take(TokenKind::Star, 1);
        case ',':
            return take(TokenKind::Comma, 1);
        case '(':
            return take(TokenKind::LeftParen, 1);
        case ')':
            return take(TokenKind::RightParen, 1);
        case ';':
            return take(TokenKind::Semicolon, 1);
        case '=':
            return take(TokenKind::Equal, 1);
        case '!':
            if (peek(1) != '=') {
                throw ParseError("expected '=' after '!'", line_, column_);
            }
            return take(TokenKind::NotEqual, 2);
        case '<':
            if (peek(1) == '=') {
                return take(TokenKind::LessEqual, 2);
            }
            return take(TokenKind::Less, 1);
        case '>':
            if (peek(1) == '=') {
                return take(TokenKind::GreaterEqual, 2);
            }
            return take(TokenKind::Greater, 1);
        default:
            throw ParseError(unexpected_character(c), line_, column_);
    }
}

std::vector<Token> tokenize(std::string_view source) {
    Lexer lexer(source);
    std::vector<Token> tokens;
    while (true) {
        tokens.push_back(lexer.next());
        if (tokens.back().kind == TokenKind::EndOfInput) {
            break;
        }
    }
    return tokens;
}

}  // namespace minidb
