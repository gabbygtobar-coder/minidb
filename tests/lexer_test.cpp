#include "minidb/lexer.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

std::vector<minidb::TokenKind> kinds_of(const std::vector<minidb::Token>& tokens) {
    std::vector<minidb::TokenKind> kinds;
    kinds.reserve(tokens.size());
    for (const minidb::Token& token : tokens) {
        kinds.push_back(token.kind);
    }
    return kinds;
}

}  // namespace

TEST(Lexer, TokenizesSelectWhere) {
    const std::vector<minidb::Token> tokens =
        minidb::tokenize("SELECT name, age FROM users WHERE age >= 21;");

    EXPECT_EQ(kinds_of(tokens),
              (std::vector<minidb::TokenKind>{
                  minidb::TokenKind::KwSelect,
                  minidb::TokenKind::Identifier,
                  minidb::TokenKind::Comma,
                  minidb::TokenKind::Identifier,
                  minidb::TokenKind::KwFrom,
                  minidb::TokenKind::Identifier,
                  minidb::TokenKind::KwWhere,
                  minidb::TokenKind::Identifier,
                  minidb::TokenKind::GreaterEqual,
                  minidb::TokenKind::IntegerLiteral,
                  minidb::TokenKind::Semicolon,
                  minidb::TokenKind::EndOfInput,
              }));
    EXPECT_EQ(tokens[1].text, "name");
    EXPECT_EQ(tokens[3].text, "age");
    EXPECT_EQ(tokens[5].text, "users");
    EXPECT_EQ(tokens[7].text, "age");
    EXPECT_EQ(tokens[9].text, "21");
}

TEST(Lexer, KeywordsAreCaseInsensitiveAndKeepSpelling) {
    const std::vector<minidb::Token> tokens = minidb::tokenize("sElEcT * FrOm t");
    ASSERT_EQ(tokens.size(), 5u);
    EXPECT_EQ(tokens[0].kind, minidb::TokenKind::KwSelect);
    EXPECT_EQ(tokens[0].text, "sElEcT");
    EXPECT_EQ(tokens[2].kind, minidb::TokenKind::KwFrom);
    EXPECT_EQ(tokens[2].text, "FrOm");
    EXPECT_EQ(tokens[3].kind, minidb::TokenKind::Identifier);
    EXPECT_EQ(tokens[3].text, "t");
}

TEST(Lexer, ScansStringsIncludingEscapedQuotes) {
    const std::vector<minidb::Token> tokens =
        minidb::tokenize("'' 'ada' 'it''s' ''''");
    ASSERT_EQ(tokens.size(), 5u);
    EXPECT_EQ(tokens[0].kind, minidb::TokenKind::StringLiteral);
    EXPECT_EQ(tokens[0].text, "");
    EXPECT_EQ(tokens[1].text, "ada");
    EXPECT_EQ(tokens[2].text, "it's");
    EXPECT_EQ(tokens[3].text, "'");
    EXPECT_EQ(tokens[4].kind, minidb::TokenKind::EndOfInput);
}

TEST(Lexer, ScansIntegersFloatsAndOperators) {
    const std::vector<minidb::Token> tokens =
        minidb::tokenize("0 42 3.14 10. .5 = != < > <= >= * , ( )");
    EXPECT_EQ(kinds_of(tokens),
              (std::vector<minidb::TokenKind>{
                  minidb::TokenKind::IntegerLiteral,
                  minidb::TokenKind::IntegerLiteral,
                  minidb::TokenKind::FloatLiteral,
                  minidb::TokenKind::FloatLiteral,
                  minidb::TokenKind::FloatLiteral,
                  minidb::TokenKind::Equal,
                  minidb::TokenKind::NotEqual,
                  minidb::TokenKind::Less,
                  minidb::TokenKind::Greater,
                  minidb::TokenKind::LessEqual,
                  minidb::TokenKind::GreaterEqual,
                  minidb::TokenKind::Star,
                  minidb::TokenKind::Comma,
                  minidb::TokenKind::LeftParen,
                  minidb::TokenKind::RightParen,
                  minidb::TokenKind::EndOfInput,
              }));
    EXPECT_EQ(tokens[2].text, "3.14");
    EXPECT_EQ(tokens[3].text, "10.");
    EXPECT_EQ(tokens[4].text, ".5");
}

TEST(Lexer, SkipsWhitespaceAndRecordsLocations) {
    const std::vector<minidb::Token> blank = minidb::tokenize(" \n\t ");
    ASSERT_EQ(blank.size(), 1u);
    EXPECT_EQ(blank[0].kind, minidb::TokenKind::EndOfInput);

    const std::vector<minidb::Token> tokens = minidb::tokenize("SELECT *");
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].line, 1u);
    EXPECT_EQ(tokens[0].column, 1u);
    EXPECT_EQ(tokens[1].kind, minidb::TokenKind::Star);
    EXPECT_EQ(tokens[1].line, 1u);
    EXPECT_EQ(tokens[1].column, 8u);
    EXPECT_EQ(tokens[2].kind, minidb::TokenKind::EndOfInput);
    EXPECT_EQ(tokens[2].column, 9u);
}

TEST(Lexer, AcceptsIdentifierShapes) {
    const std::vector<minidb::Token> tokens = minidb::tokenize("_id a1");
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].kind, minidb::TokenKind::Identifier);
    EXPECT_EQ(tokens[0].text, "_id");
    EXPECT_EQ(tokens[1].kind, minidb::TokenKind::Identifier);
    EXPECT_EQ(tokens[1].text, "a1");
}

TEST(Lexer, RejectsUnexpectedCharacterWithLocation) {
    try {
        minidb::tokenize("SELECT *\nFROM @t");
        FAIL() << "expected ParseError";
    } catch (const minidb::ParseError& error) {
        EXPECT_EQ(error.line(), 2u);
        EXPECT_EQ(error.column(), 6u);
        EXPECT_EQ(std::string(error.what()), "unexpected character '@'");
    }
}

TEST(Lexer, RejectsBareExclamationAndDoubleQuote) {
    try {
        minidb::tokenize("!");
        FAIL() << "expected ParseError";
    } catch (const minidb::ParseError& error) {
        EXPECT_EQ(error.line(), 1u);
        EXPECT_EQ(error.column(), 1u);
        EXPECT_EQ(std::string(error.what()), "expected '=' after '!'");
    }

    EXPECT_THROW(minidb::tokenize("\""), minidb::ParseError);
}

TEST(Lexer, RejectsUnterminatedStringAtOpeningQuote) {
    const std::string sql = "INSERT INTO t VALUES ('hi";
    const std::size_t column = sql.find('\'') + 1;
    try {
        minidb::tokenize(sql);
        FAIL() << "expected ParseError";
    } catch (const minidb::ParseError& error) {
        EXPECT_EQ(error.line(), 1u);
        EXPECT_EQ(error.column(), column);
        EXPECT_EQ(std::string(error.what()), "unterminated string literal");
    }
}
