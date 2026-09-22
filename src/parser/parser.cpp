#include "minidb/parser.hpp"

#include "minidb/lexer.hpp"

#include <charconv>
#include <system_error>
#include <utility>

namespace minidb {
namespace {

std::string describe(const Token& token) {
    switch (token.kind) {
        case TokenKind::EndOfInput:
            return "end of input";
        case TokenKind::Identifier:
            return "identifier '" + token.text + "'";
        case TokenKind::IntegerLiteral:
            return "integer '" + token.text + "'";
        case TokenKind::FloatLiteral:
            return "float '" + token.text + "'";
        case TokenKind::StringLiteral:
            return "string '" + token.text + "'";
        case TokenKind::KwCreate:
            return "'CREATE'";
        case TokenKind::KwTable:
            return "'TABLE'";
        case TokenKind::KwDrop:
            return "'DROP'";
        case TokenKind::KwInsert:
            return "'INSERT'";
        case TokenKind::KwInto:
            return "'INTO'";
        case TokenKind::KwValues:
            return "'VALUES'";
        case TokenKind::KwSelect:
            return "'SELECT'";
        case TokenKind::KwFrom:
            return "'FROM'";
        case TokenKind::KwWhere:
            return "'WHERE'";
        case TokenKind::KwUpdate:
            return "'UPDATE'";
        case TokenKind::KwSet:
            return "'SET'";
        case TokenKind::KwDelete:
            return "'DELETE'";
        case TokenKind::KwInt:
            return "'INT'";
        case TokenKind::KwText:
            return "'TEXT'";
        case TokenKind::KwBoolean:
            return "'BOOLEAN'";
        case TokenKind::KwFloat:
            return "'FLOAT'";
        case TokenKind::KwTrue:
            return "'TRUE'";
        case TokenKind::KwFalse:
            return "'FALSE'";
        case TokenKind::KwIndex:
            return "'INDEX'";
        case TokenKind::KwOn:
            return "'ON'";
        case TokenKind::Star:
            return "'*'";
        case TokenKind::Comma:
            return "','";
        case TokenKind::LeftParen:
            return "'('";
        case TokenKind::RightParen:
            return "')'";
        case TokenKind::Semicolon:
            return "';'";
        case TokenKind::Equal:
            return "'='";
        case TokenKind::NotEqual:
            return "'!='";
        case TokenKind::Less:
            return "'<'";
        case TokenKind::Greater:
            return "'>'";
        case TokenKind::LessEqual:
            return "'<='";
        case TokenKind::GreaterEqual:
            return "'>='";
    }
    return "token";
}

class Parser {
  public:
    explicit Parser(std::string_view source) : lexer_(source), current_(lexer_.next()) {}

    Statement parse() {
        Statement statement = parse_statement_body();
        if (check(TokenKind::Semicolon)) {
            advance();
        }
        if (!check(TokenKind::EndOfInput)) {
            fail("unexpected token after statement: " + describe(current_));
        }
        return statement;
    }

  private:
    [[noreturn]] void fail(const std::string& message) const {
        throw ParseError(message, current_.line, current_.column);
    }

    bool check(TokenKind kind) const { return current_.kind == kind; }

    void advance() { current_ = lexer_.next(); }

    bool match(TokenKind kind) {
        if (!check(kind)) {
            return false;
        }
        advance();
        return true;
    }

    void expect(TokenKind kind, std::string_view what) {
        if (!check(kind)) {
            fail("expected " + std::string(what) + ", found " + describe(current_));
        }
        advance();
    }

    std::string parse_identifier(std::string_view what) {
        if (!check(TokenKind::Identifier)) {
            fail("expected " + std::string(what) + ", found " + describe(current_));
        }
        std::string name = current_.text;
        advance();
        return name;
    }

    DataType parse_data_type() {
        if (match(TokenKind::KwInt)) {
            return DataType::Int;
        }
        if (match(TokenKind::KwText)) {
            return DataType::Text;
        }
        if (match(TokenKind::KwBoolean)) {
            return DataType::Boolean;
        }
        if (match(TokenKind::KwFloat)) {
            return DataType::Float;
        }
        fail("expected a data type, found " + describe(current_));
    }

    Literal parse_literal() {
        if (check(TokenKind::IntegerLiteral)) {
            return parse_integer(current_);
        }
        if (check(TokenKind::FloatLiteral)) {
            return parse_float(current_);
        }
        if (check(TokenKind::StringLiteral)) {
            Literal literal;
            literal.kind = LiteralKind::String;
            literal.text = current_.text;
            advance();
            return literal;
        }
        if (match(TokenKind::KwTrue)) {
            Literal literal;
            literal.kind = LiteralKind::Boolean;
            literal.boolean = true;
            literal.text = "TRUE";
            return literal;
        }
        if (match(TokenKind::KwFalse)) {
            Literal literal;
            literal.kind = LiteralKind::Boolean;
            literal.boolean = false;
            literal.text = "FALSE";
            return literal;
        }
        fail("expected a literal, found " + describe(current_));
    }

    Literal parse_integer(const Token& token) {
        const Token number = token;
        advance();
        std::int64_t value = 0;
        const char* const begin = number.text.data();
        const char* const end = begin + number.text.size();
        const std::from_chars_result result = std::from_chars(begin, end, value);
        if (result.ec == std::errc::result_out_of_range) {
            throw ParseError("integer literal out of range", number.line, number.column);
        }
        if (result.ec != std::errc() || result.ptr != end) {
            throw ParseError("invalid integer literal", number.line, number.column);
        }
        Literal literal;
        literal.kind = LiteralKind::Integer;
        literal.text = number.text;
        literal.integer = value;
        return literal;
    }

    Literal parse_float(const Token& token) {
        const Token number = token;
        advance();
        double value = 0.0;
        const char* const begin = number.text.data();
        const char* const end = begin + number.text.size();
        const std::from_chars_result result = std::from_chars(begin, end, value);
        if (result.ec == std::errc::result_out_of_range) {
            throw ParseError("floating-point literal out of range", number.line, number.column);
        }
        if (result.ec != std::errc() || result.ptr != end) {
            throw ParseError("invalid floating-point literal", number.line, number.column);
        }
        Literal literal;
        literal.kind = LiteralKind::Float;
        literal.text = number.text;
        literal.floating = value;
        return literal;
    }

    ComparisonOp parse_comparison() {
        const TokenKind kind = current_.kind;
        switch (kind) {
            case TokenKind::Equal:
                advance();
                return ComparisonOp::Equal;
            case TokenKind::NotEqual:
                advance();
                return ComparisonOp::NotEqual;
            case TokenKind::Less:
                advance();
                return ComparisonOp::Less;
            case TokenKind::Greater:
                advance();
                return ComparisonOp::Greater;
            case TokenKind::LessEqual:
                advance();
                return ComparisonOp::LessEqual;
            case TokenKind::GreaterEqual:
                advance();
                return ComparisonOp::GreaterEqual;
            default:
                fail("expected a comparison operator, found " + describe(current_));
        }
    }

    WhereClause parse_where() {
        WhereClause where;
        where.column = parse_identifier("a column name");
        where.op = parse_comparison();
        where.value = parse_literal();
        return where;
    }

    CreateTableStatement parse_create_table() {
        expect(TokenKind::KwTable, "TABLE");
        CreateTableStatement statement;
        statement.name = parse_identifier("a table name");
        expect(TokenKind::LeftParen, "'('");
        do {
            ColumnDefinition column;
            column.name = parse_identifier("a column name");
            column.type = parse_data_type();
            statement.columns.push_back(std::move(column));
        } while (match(TokenKind::Comma));
        expect(TokenKind::RightParen, "')'");
        return statement;
    }

    DropTableStatement parse_drop_table() {
        expect(TokenKind::KwTable, "TABLE");
        DropTableStatement statement;
        statement.name = parse_identifier("a table name");
        return statement;
    }

    InsertStatement parse_insert() {
        expect(TokenKind::KwInto, "INTO");
        InsertStatement statement;
        statement.table = parse_identifier("a table name");
        expect(TokenKind::KwValues, "VALUES");
        expect(TokenKind::LeftParen, "'('");
        do {
            statement.values.push_back(parse_literal());
        } while (match(TokenKind::Comma));
        expect(TokenKind::RightParen, "')'");
        return statement;
    }

    SelectStatement parse_select() {
        SelectStatement statement;
        if (match(TokenKind::Star)) {
            statement.select_all = true;
        } else if (check(TokenKind::Identifier)) {
            statement.columns.push_back(parse_identifier("a column name"));
            while (match(TokenKind::Comma)) {
                statement.columns.push_back(parse_identifier("a column name"));
            }
        } else {
            fail("expected '*' or a column name, found " + describe(current_));
        }
        expect(TokenKind::KwFrom, "FROM");
        statement.table = parse_identifier("a table name");
        if (match(TokenKind::KwWhere)) {
            statement.where = parse_where();
        }
        return statement;
    }

    UpdateStatement parse_update() {
        UpdateStatement statement;
        statement.table = parse_identifier("a table name");
        expect(TokenKind::KwSet, "SET");
        statement.column = parse_identifier("a column name");
        expect(TokenKind::Equal, "'='");
        statement.value = parse_literal();
        if (match(TokenKind::KwWhere)) {
            statement.where = parse_where();
        }
        return statement;
    }

    CreateIndexStatement parse_create_index() {
        expect(TokenKind::KwIndex, "INDEX");
        CreateIndexStatement statement;
        statement.name = parse_identifier("an index name");
        expect(TokenKind::KwOn, "ON");
        statement.table = parse_identifier("a table name");
        expect(TokenKind::LeftParen, "'('");
        statement.column = parse_identifier("a column name");
        expect(TokenKind::RightParen, "')'");
        return statement;
    }

    DropIndexStatement parse_drop_index() {
        expect(TokenKind::KwIndex, "INDEX");
        DropIndexStatement statement;
        statement.name = parse_identifier("an index name");
        return statement;
    }

    DeleteStatement parse_delete() {
        expect(TokenKind::KwFrom, "FROM");
        DeleteStatement statement;
        statement.table = parse_identifier("a table name");
        if (match(TokenKind::KwWhere)) {
            statement.where = parse_where();
        }
        return statement;
    }

    Statement parse_statement_body() {
        if (match(TokenKind::KwCreate)) {
            if (check(TokenKind::KwIndex)) {
                return parse_create_index();
            }
            return parse_create_table();
        }
        if (match(TokenKind::KwDrop)) {
            if (check(TokenKind::KwIndex)) {
                return parse_drop_index();
            }
            return parse_drop_table();
        }
        if (match(TokenKind::KwInsert)) {
            return parse_insert();
        }
        if (match(TokenKind::KwSelect)) {
            return parse_select();
        }
        if (match(TokenKind::KwUpdate)) {
            return parse_update();
        }
        if (match(TokenKind::KwDelete)) {
            return parse_delete();
        }
        fail("expected a SQL statement, found " + describe(current_));
    }

    Lexer lexer_;
    Token current_;
};

}  // namespace

Statement parse_statement(std::string_view source) {
    Parser parser(source);
    return parser.parse();
}

}  // namespace minidb
