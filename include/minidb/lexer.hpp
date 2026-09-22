#pragma once

#include "minidb/parse_error.hpp"
#include "minidb/token.hpp"

#include <string_view>
#include <vector>

namespace minidb {

// Scanner for the MiniDB SQL subset. The lexer does not know statement grammar.
// `next` throws ParseError on input that is not a token.
class Lexer {
  public:
    explicit Lexer(std::string_view source);

    Token next();

  private:
    bool at_end() const;
    char peek(std::size_t ahead = 0) const;
    void advance();
    void skip_whitespace();

    Token take(TokenKind kind, std::size_t length);
    Token identifier();
    Token number();
    Token string_literal();

    std::string_view source_;
    std::size_t pos_ = 0;
    std::size_t line_ = 1;
    std::size_t column_ = 1;
};

// Every token from `source`, including the terminating EndOfInput token.
// Throws ParseError on the first invalid lexeme.
std::vector<Token> tokenize(std::string_view source);

}  // namespace minidb
