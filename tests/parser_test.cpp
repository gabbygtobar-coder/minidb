#include "minidb/parser.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>

namespace {

minidb::Statement parse(const std::string& sql) {
    return minidb::parse_statement(sql);
}

}  // namespace

TEST(Parser, CreateTable) {
    const minidb::Statement statement =
        parse("CREATE TABLE users (id INT, name TEXT, active BOOLEAN, score FLOAT);");
    const auto* create = std::get_if<minidb::CreateTableStatement>(&statement);
    ASSERT_NE(create, nullptr);
    EXPECT_EQ(create->name, "users");
    ASSERT_EQ(create->columns.size(), 4u);
    EXPECT_EQ(create->columns[0].name, "id");
    EXPECT_EQ(create->columns[0].type, minidb::DataType::Int);
    EXPECT_EQ(create->columns[1].name, "name");
    EXPECT_EQ(create->columns[1].type, minidb::DataType::Text);
    EXPECT_EQ(create->columns[2].name, "active");
    EXPECT_EQ(create->columns[2].type, minidb::DataType::Boolean);
    EXPECT_EQ(create->columns[3].name, "score");
    EXPECT_EQ(create->columns[3].type, minidb::DataType::Float);
    EXPECT_EQ(minidb::format_statement(statement),
              "CreateTable users (id INT, name TEXT, active BOOLEAN, score FLOAT)");
}

TEST(Parser, TypeKeywordsAreCaseInsensitiveAndNamesKeepCase) {
    const minidb::Statement statement =
        parse("CrEaTe TaBlE Users (UserId InT, Name tExT, Flag BoOlEaN, Ratio fLoAt)");
    const auto* create = std::get_if<minidb::CreateTableStatement>(&statement);
    ASSERT_NE(create, nullptr);
    EXPECT_EQ(create->name, "Users");
    ASSERT_EQ(create->columns.size(), 4u);
    EXPECT_EQ(create->columns[0].name, "UserId");
    EXPECT_EQ(create->columns[0].type, minidb::DataType::Int);
    EXPECT_EQ(create->columns[1].type, minidb::DataType::Text);
    EXPECT_EQ(create->columns[2].type, minidb::DataType::Boolean);
    EXPECT_EQ(create->columns[3].type, minidb::DataType::Float);
    EXPECT_EQ(minidb::format_statement(statement),
              "CreateTable Users (UserId INT, Name TEXT, Flag BOOLEAN, Ratio FLOAT)");
}

TEST(Parser, DropTable) {
    const minidb::Statement statement = parse("DROP TABLE users");
    const auto* drop = std::get_if<minidb::DropTableStatement>(&statement);
    ASSERT_NE(drop, nullptr);
    EXPECT_EQ(drop->name, "users");
    EXPECT_EQ(minidb::format_statement(statement), "DropTable users");
    EXPECT_EQ(minidb::format_statement(parse("drop table users;")), "DropTable users");
}

TEST(Parser, InsertLiterals) {
    const minidb::Statement statement =
        parse("INSERT INTO users VALUES (007, 'it''s', TRUE, 3.14, false, .5, 10.);");
    const auto* insert = std::get_if<minidb::InsertStatement>(&statement);
    ASSERT_NE(insert, nullptr);
    EXPECT_EQ(insert->table, "users");
    ASSERT_EQ(insert->values.size(), 7u);

    EXPECT_EQ(insert->values[0].kind, minidb::LiteralKind::Integer);
    EXPECT_EQ(insert->values[0].integer, 7);
    EXPECT_EQ(insert->values[0].text, "007");

    EXPECT_EQ(insert->values[1].kind, minidb::LiteralKind::String);
    EXPECT_EQ(insert->values[1].text, "it's");

    EXPECT_EQ(insert->values[2].kind, minidb::LiteralKind::Boolean);
    EXPECT_TRUE(insert->values[2].boolean);

    EXPECT_EQ(insert->values[3].kind, minidb::LiteralKind::Float);
    EXPECT_DOUBLE_EQ(insert->values[3].floating, 3.14);
    EXPECT_EQ(insert->values[3].text, "3.14");

    EXPECT_EQ(insert->values[4].kind, minidb::LiteralKind::Boolean);
    EXPECT_FALSE(insert->values[4].boolean);

    EXPECT_EQ(insert->values[5].kind, minidb::LiteralKind::Float);
    EXPECT_DOUBLE_EQ(insert->values[5].floating, 0.5);
    EXPECT_EQ(insert->values[5].text, ".5");

    EXPECT_EQ(insert->values[6].kind, minidb::LiteralKind::Float);
    EXPECT_DOUBLE_EQ(insert->values[6].floating, 10.0);
    EXPECT_EQ(insert->values[6].text, "10.");

    EXPECT_EQ(minidb::format_statement(statement),
              "Insert users VALUES (007, 'it''s', TRUE, 3.14, FALSE, .5, 10.)");
}

TEST(Parser, IntegerLiteralRange) {
    const minidb::Statement statement =
        parse("INSERT INTO t VALUES (9223372036854775807)");
    const auto* insert = std::get_if<minidb::InsertStatement>(&statement);
    ASSERT_NE(insert, nullptr);
    ASSERT_EQ(insert->values.size(), 1u);
    EXPECT_EQ(insert->values[0].integer, std::numeric_limits<std::int64_t>::max());

    try {
        parse("INSERT INTO t VALUES (9223372036854775808)");
        FAIL() << "expected ParseError";
    } catch (const minidb::ParseError& error) {
        EXPECT_EQ(std::string(error.what()), "integer literal out of range");
        EXPECT_EQ(error.line(), 1u);
    }
}

TEST(Parser, SelectStarAndColumns) {
    const minidb::Statement star = parse("SELECT * FROM users");
    const auto* star_stmt = std::get_if<minidb::SelectStatement>(&star);
    ASSERT_NE(star_stmt, nullptr);
    EXPECT_TRUE(star_stmt->select_all);
    EXPECT_TRUE(star_stmt->columns.empty());
    EXPECT_EQ(star_stmt->table, "users");
    EXPECT_FALSE(star_stmt->where.has_value());
    EXPECT_EQ(minidb::format_statement(star), "Select * FROM users");

    const minidb::Statement columns = parse("select id, name from users;");
    const auto* col_stmt = std::get_if<minidb::SelectStatement>(&columns);
    ASSERT_NE(col_stmt, nullptr);
    EXPECT_FALSE(col_stmt->select_all);
    ASSERT_EQ(col_stmt->columns.size(), 2u);
    EXPECT_EQ(col_stmt->columns[0], "id");
    EXPECT_EQ(col_stmt->columns[1], "name");
    EXPECT_EQ(minidb::format_statement(columns), "Select id, name FROM users");
}

TEST(Parser, WhereComparisonOperators) {
    struct Case {
        const char* sql_op;
        minidb::ComparisonOp op;
        const char* shown;
    };
    const Case cases[] = {
        {"=", minidb::ComparisonOp::Equal, "="},
        {"!=", minidb::ComparisonOp::NotEqual, "!="},
        {"<", minidb::ComparisonOp::Less, "<"},
        {">", minidb::ComparisonOp::Greater, ">"},
        {"<=", minidb::ComparisonOp::LessEqual, "<="},
        {">=", minidb::ComparisonOp::GreaterEqual, ">="},
    };
    for (const Case& item : cases) {
        const std::string sql =
            std::string("SELECT * FROM t WHERE age ") + item.sql_op + " 21";
        const minidb::Statement statement = parse(sql);
        const auto* select = std::get_if<minidb::SelectStatement>(&statement);
        ASSERT_NE(select, nullptr) << sql;
        ASSERT_TRUE(select->where.has_value()) << sql;
        EXPECT_EQ(select->where->column, "age") << sql;
        EXPECT_EQ(select->where->op, item.op) << sql;
        EXPECT_EQ(select->where->value.kind, minidb::LiteralKind::Integer) << sql;
        EXPECT_EQ(select->where->value.integer, 21) << sql;
        EXPECT_EQ(minidb::format_statement(statement),
                  std::string("Select * FROM t WHERE age ") + item.shown + " 21")
            << sql;
    }
}

TEST(Parser, WhereAcceptsStringFloatAndBoolean) {
    const minidb::Statement statement =
        parse("SELECT name FROM users WHERE name = 'ada'");
    const auto* select = std::get_if<minidb::SelectStatement>(&statement);
    ASSERT_NE(select, nullptr);
    ASSERT_TRUE(select->where.has_value());
    EXPECT_EQ(select->where->value.kind, minidb::LiteralKind::String);
    EXPECT_EQ(select->where->value.text, "ada");

    EXPECT_EQ(minidb::format_statement(parse("SELECT * FROM t WHERE score >= 3.14")),
              "Select * FROM t WHERE score >= 3.14");
    EXPECT_EQ(minidb::format_statement(parse("SELECT * FROM t WHERE active = FALSE")),
              "Select * FROM t WHERE active = FALSE");
}

TEST(Parser, UpdateAndDelete) {
    const minidb::Statement update =
        parse("UPDATE users SET name = 'ada lovelace' WHERE id = 1");
    const auto* update_stmt = std::get_if<minidb::UpdateStatement>(&update);
    ASSERT_NE(update_stmt, nullptr);
    EXPECT_EQ(update_stmt->table, "users");
    EXPECT_EQ(update_stmt->column, "name");
    EXPECT_EQ(update_stmt->value.kind, minidb::LiteralKind::String);
    EXPECT_EQ(update_stmt->value.text, "ada lovelace");
    ASSERT_TRUE(update_stmt->where.has_value());
    EXPECT_EQ(update_stmt->where->column, "id");
    EXPECT_EQ(update_stmt->where->op, minidb::ComparisonOp::Equal);
    EXPECT_EQ(minidb::format_statement(update),
              "Update users SET name = 'ada lovelace' WHERE id = 1");

    const minidb::Statement update_all = parse("UPDATE users SET active = TRUE;");
    const auto* all = std::get_if<minidb::UpdateStatement>(&update_all);
    ASSERT_NE(all, nullptr);
    EXPECT_FALSE(all->where.has_value());
    EXPECT_TRUE(all->value.boolean);
    EXPECT_EQ(minidb::format_statement(update_all), "Update users SET active = TRUE");

    const minidb::Statement deleted = parse("DELETE FROM users");
    const auto* delete_stmt = std::get_if<minidb::DeleteStatement>(&deleted);
    ASSERT_NE(delete_stmt, nullptr);
    EXPECT_EQ(delete_stmt->table, "users");
    EXPECT_FALSE(delete_stmt->where.has_value());
    EXPECT_EQ(minidb::format_statement(deleted), "Delete FROM users");

    EXPECT_EQ(minidb::format_statement(parse("DELETE FROM users WHERE id != 2;")),
              "Delete FROM users WHERE id != 2");
}

TEST(Parser, AllowsNewlinesInsideOneStatement) {
    EXPECT_EQ(minidb::format_statement(parse("SELECT *\nFROM users\nWHERE id = 1")),
              "Select * FROM users WHERE id = 1");
}

TEST(Parser, ReportsLocationOnBadSyntax) {
    try {
        parse("CREATE TABLE t (id INTEGER)");
        FAIL() << "expected ParseError";
    } catch (const minidb::ParseError& error) {
        EXPECT_EQ(error.line(), 1u);
        EXPECT_EQ(error.column(), 20u);
        EXPECT_NE(std::string(error.what()).find("data type"), std::string::npos);
        EXPECT_NE(std::string(error.what()).find("INTEGER"), std::string::npos);
    }

    try {
        parse("SELECT *\nFROM");
        FAIL() << "expected ParseError";
    } catch (const minidb::ParseError& error) {
        EXPECT_EQ(error.line(), 2u);
        EXPECT_EQ(error.column(), 5u);
        EXPECT_NE(std::string(error.what()).find("table name"), std::string::npos);
    }
}

TEST(Parser, LexerErrorsPropagate) {
    try {
        parse("SELECT * FROM t WHERE name = '");
        FAIL() << "expected ParseError";
    } catch (const minidb::ParseError& error) {
        EXPECT_EQ(std::string(error.what()), "unterminated string literal");
        EXPECT_EQ(error.line(), 1u);
    }
}

TEST(Parser, RejectsBadSyntax) {
    const char* programs[] = {
        "",
        ";",
        "SELECT 1",
        "SELECT *",
        "SELECT * FROM",
        "SELECT FROM t",
        "CREATE TABLE",
        "CREATE TABLE t",
        "CREATE TABLE t ()",
        "CREATE TABLE t (id)",
        "CREATE TABLE t (id INTEGER)",
        "DROP t",
        "DROP TABLE",
        "DROP TABLE select",
        "INSERT t VALUES (1)",
        "INSERT INTO t (1)",
        "INSERT INTO t VALUES ()",
        "INSERT INTO t VALUES (1), (2)",
        "INSERT INTO t VALUES (1.2.3)",
        "UPDATE t",
        "UPDATE t SET",
        "UPDATE t SET a",
        "UPDATE t SET a =",
        "UPDATE t SET a = 1, b = 2",
        "DELETE t",
        "DELETE FROM",
        "SELECT * FROM t WHERE",
        "SELECT * FROM t WHERE a",
        "SELECT * FROM t WHERE a == 1",
        "SELECT * FROM t WHERE a =",
        "SELECT * FROM t; SELECT * FROM t",
        "SELECT * FROM t;;",
        "SELECT * FROM t extra",
    };
    for (const char* sql : programs) {
        EXPECT_THROW(parse(sql), minidb::ParseError) << sql;
    }
}
