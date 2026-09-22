#include "minidb/catalog.hpp"
#include "minidb/execution_error.hpp"
#include "minidb/executor.hpp"
#include "minidb/parser.hpp"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

namespace {

minidb::StatementResult exec(minidb::Database& database, const std::string& sql) {
    return minidb::execute(database, minidb::parse_statement(sql));
}

void expect_error(minidb::Database& database, const std::string& sql, const std::string& message) {
    try {
        exec(database, sql);
        FAIL() << "expected ExecutionError for: " << sql;
    } catch (const minidb::ExecutionError& error) {
        EXPECT_EQ(std::string(error.what()), message) << sql;
    }
}

minidb::ResultSet rows_of(minidb::StatementResult outcome) {
    EXPECT_TRUE(outcome.message.empty());
    EXPECT_TRUE(outcome.result.has_value());
    if (!outcome.result.has_value()) {
        return {};
    }
    return std::move(*outcome.result);
}

}  // namespace

TEST(Executor, CreateInsertSelectAndSchema) {
    minidb::Database database;
    EXPECT_EQ(exec(database, "CREATE TABLE users (id INT, name TEXT, active BOOLEAN, score FLOAT);")
                  .message,
              "Created table users.");
    EXPECT_EQ(database.table_names(), std::vector<std::string>{"users"});
    EXPECT_EQ(minidb::format_schema(database.require_table("users")),
              "CREATE TABLE users (id INT, name TEXT, active BOOLEAN, score FLOAT);");

    EXPECT_EQ(exec(database, "INSERT INTO users VALUES (1, 'ada', TRUE, 3.14);").message,
              "Inserted 1 row.");
    EXPECT_EQ(exec(database, "INSERT INTO users VALUES (2, 'grace', false, 10.);").message,
              "Inserted 1 row.");

    const minidb::StatementResult selected = exec(database, "SELECT * FROM users");
    const minidb::ResultSet result = rows_of(selected);
    EXPECT_EQ(result.column_names, (std::vector<std::string>{"id", "name", "active", "score"}));
    ASSERT_EQ(result.rows.size(), 2u);
    EXPECT_EQ(result.rows[0], (std::vector<std::string>{"1", "ada", "TRUE", "3.14"}));
    EXPECT_EQ(result.rows[1], (std::vector<std::string>{"2", "grace", "FALSE", "10.0"}));
    EXPECT_EQ(minidb::format_result_set(result),
              "id | name  | active | score\n"
              "---+-------+--------+------\n"
              "1  | ada   | TRUE   | 3.14\n"
              "2  | grace | FALSE  | 10.0\n"
              "(2 rows)");
}

TEST(Executor, SelectProjectsColumnsAndPreservesOrder) {
    minidb::Database database;
    exec(database, "CREATE TABLE users (id INT, name TEXT)");
    exec(database, "INSERT INTO users VALUES (1, 'ada')");
    exec(database, "INSERT INTO users VALUES (2, 'grace')");

    const minidb::ResultSet result = rows_of(exec(database, "SELECT name, id FROM users"));
    EXPECT_EQ(result.column_names, (std::vector<std::string>{"name", "id"}));
    ASSERT_EQ(result.rows.size(), 2u);
    EXPECT_EQ(result.rows[0], (std::vector<std::string>{"ada", "1"}));
    EXPECT_EQ(result.rows[1], (std::vector<std::string>{"grace", "2"}));
    EXPECT_EQ(minidb::format_result_set(rows_of(exec(database, "SELECT * FROM users WHERE id = 99"))),
              "id | name\n"
              "---+-----\n"
              "(0 rows)");
}

TEST(Executor, WhereFiltersIntsFloatsTextAndBooleans) {
    minidb::Database database;
    exec(database, "CREATE TABLE nums (n INT, score FLOAT, name TEXT, active BOOLEAN)");
    exec(database, "INSERT INTO nums VALUES (1, 1.5, 'ada', TRUE)");
    exec(database, "INSERT INTO nums VALUES (2, 2.0, 'Ada', FALSE)");
    exec(database, "INSERT INTO nums VALUES (3, 3.5, 'grace', TRUE)");
    exec(database, "INSERT INTO nums VALUES (10, 10.0, 'ada', FALSE)");

    EXPECT_EQ(rows_of(exec(database, "SELECT n FROM nums WHERE n = 2")).rows,
              (std::vector<std::vector<std::string>>{{"2"}}));
    EXPECT_EQ(rows_of(exec(database, "SELECT n FROM nums WHERE n != 2")).rows,
              (std::vector<std::vector<std::string>>{{"1"}, {"3"}, {"10"}}));
    EXPECT_EQ(rows_of(exec(database, "SELECT n FROM nums WHERE n < 3")).rows,
              (std::vector<std::vector<std::string>>{{"1"}, {"2"}}));
    EXPECT_EQ(rows_of(exec(database, "SELECT n FROM nums WHERE n > 3")).rows,
              (std::vector<std::vector<std::string>>{{"10"}}));
    EXPECT_EQ(rows_of(exec(database, "SELECT n FROM nums WHERE n <= 2")).rows,
              (std::vector<std::vector<std::string>>{{"1"}, {"2"}}));
    EXPECT_EQ(rows_of(exec(database, "SELECT n FROM nums WHERE n >= 10")).rows,
              (std::vector<std::vector<std::string>>{{"10"}}));

    EXPECT_EQ(rows_of(exec(database, "SELECT n FROM nums WHERE score < 2.0")).rows,
              (std::vector<std::vector<std::string>>{{"1"}}));
    EXPECT_EQ(rows_of(exec(database, "SELECT n FROM nums WHERE score >= 3.5")).rows,
              (std::vector<std::vector<std::string>>{{"3"}, {"10"}}));
    EXPECT_EQ(rows_of(exec(database, "SELECT n FROM nums WHERE score = 10.0")).rows,
              (std::vector<std::vector<std::string>>{{"10"}}));

    EXPECT_EQ(rows_of(exec(database, "SELECT n FROM nums WHERE name = 'ada'")).rows,
              (std::vector<std::vector<std::string>>{{"1"}, {"10"}}));
    EXPECT_EQ(rows_of(exec(database, "SELECT n FROM nums WHERE name != 'ada'")).rows,
              (std::vector<std::vector<std::string>>{{"2"}, {"3"}}));

    EXPECT_EQ(rows_of(exec(database, "SELECT n FROM nums WHERE active = TRUE")).rows,
              (std::vector<std::vector<std::string>>{{"1"}, {"3"}}));
    EXPECT_EQ(rows_of(exec(database, "SELECT n FROM nums WHERE active != FALSE")).rows,
              (std::vector<std::vector<std::string>>{{"1"}, {"3"}}));
}

TEST(Executor, RejectsOrderingOnTextAndBoolean) {
    minidb::Database database;
    exec(database, "CREATE TABLE t (name TEXT, active BOOLEAN)");
    exec(database, "INSERT INTO t VALUES ('ada', TRUE)");

    expect_error(database, "SELECT * FROM t WHERE name < 'b'",
                 "Operator < is not supported for TEXT columns (only = and !=)");
    expect_error(database, "SELECT * FROM t WHERE name > 'a'",
                 "Operator > is not supported for TEXT columns (only = and !=)");
    expect_error(database, "SELECT * FROM t WHERE name <= 'ada'",
                 "Operator <= is not supported for TEXT columns (only = and !=)");
    expect_error(database, "SELECT * FROM t WHERE name >= 'ada'",
                 "Operator >= is not supported for TEXT columns (only = and !=)");
    expect_error(database, "SELECT * FROM t WHERE active < TRUE",
                 "Operator < is not supported for BOOLEAN columns (only = and !=)");
    expect_error(database, "SELECT * FROM t WHERE active > FALSE",
                 "Operator > is not supported for BOOLEAN columns (only = and !=)");
    expect_error(database, "DELETE FROM t WHERE name < 'z'",
                 "Operator < is not supported for TEXT columns (only = and !=)");
    EXPECT_EQ(database.row_count("t"), 1u);
}

TEST(Executor, TypeMismatchAndArityLeaveTheTableUnchanged) {
    minidb::Database database;
    exec(database, "CREATE TABLE users (id INT, name TEXT, active BOOLEAN, score FLOAT)");
    exec(database, "INSERT INTO users VALUES (1, 'ada', TRUE, 1.5)");

    expect_error(database, "INSERT INTO users VALUES (2, 'grace')",
                 "INSERT INTO users expected 4 values, got 2");
    expect_error(database, "INSERT INTO users VALUES (2, 'grace', TRUE, 1.0, 5)",
                 "INSERT INTO users expected 4 values, got 5");
    expect_error(database, "INSERT INTO users VALUES ('x', 'grace', TRUE, 1.0)",
                 "Type mismatch for column id: expected INT, got TEXT");
    expect_error(database, "INSERT INTO users VALUES (1.0, 'grace', TRUE, 1.0)",
                 "Type mismatch for column id: expected INT, got FLOAT");
    expect_error(database, "INSERT INTO users VALUES (2, 3, TRUE, 1.0)",
                 "Type mismatch for column name: expected TEXT, got INT");
    expect_error(database, "INSERT INTO users VALUES (2, 'grace', 1, 1.0)",
                 "Type mismatch for column active: expected BOOLEAN, got INT");
    expect_error(database, "INSERT INTO users VALUES (2, 'grace', TRUE, 1)",
                 "Type mismatch for column score: expected FLOAT, got INT");
    expect_error(database, "SELECT * FROM users WHERE id = '1'",
                 "Type mismatch for column id: expected INT, got TEXT");
    expect_error(database, "SELECT * FROM users WHERE score = 1",
                 "Type mismatch for column score: expected FLOAT, got INT");
    expect_error(database, "SELECT * FROM users WHERE name = TRUE",
                 "Type mismatch for column name: expected TEXT, got BOOLEAN");
    expect_error(database, "UPDATE users SET id = 'no' WHERE id = 1",
                 "Type mismatch for column id: expected INT, got TEXT");

    EXPECT_EQ(database.row_count("users"), 1u);
    EXPECT_EQ(rows_of(exec(database, "SELECT id, name FROM users")).rows,
              (std::vector<std::vector<std::string>>{{"1", "ada"}}));
}

TEST(Executor, UnknownNamesAndDuplicateTable) {
    minidb::Database database;
    exec(database, "CREATE TABLE users (id INT, name TEXT)");
    exec(database, "INSERT INTO users VALUES (1, 'ada')");

    expect_error(database, "CREATE TABLE users (id INT)", "Table already exists: users");
    EXPECT_EQ(database.row_count("users"), 1u);

    expect_error(database, "INSERT INTO missing VALUES (1)", "No such table: missing");
    expect_error(database, "SELECT * FROM missing", "No such table: missing");
    expect_error(database, "UPDATE missing SET id = 1", "No such table: missing");
    expect_error(database, "DELETE FROM missing", "No such table: missing");
    expect_error(database, "DROP TABLE missing", "No such table: missing");
    expect_error(database, "SELECT nope FROM users", "Unknown column: nope");
    expect_error(database, "SELECT * FROM users WHERE nope = 1", "Unknown column: nope");
    expect_error(database, "UPDATE users SET nope = 1", "Unknown column: nope");

    minidb::Database direct;
    EXPECT_THROW(direct.create_table("t", {}), minidb::ExecutionError);
    try {
        direct.create_table("t", {{"id", minidb::DataType::Int}, {"id", minidb::DataType::Text}});
        FAIL() << "expected duplicate column";
    } catch (const minidb::ExecutionError& error) {
        EXPECT_EQ(std::string(error.what()), "Duplicate column name: id");
    }
    expect_error(database, "CREATE TABLE bad (id INT, id TEXT)", "Duplicate column name: id");
}

TEST(Executor, UpdateAndDeleteHonorWhere) {
    minidb::Database database;
    exec(database, "CREATE TABLE users (id INT, name TEXT, active BOOLEAN)");
    exec(database, "INSERT INTO users VALUES (1, 'ada', TRUE)");
    exec(database, "INSERT INTO users VALUES (2, 'grace', FALSE)");
    exec(database, "INSERT INTO users VALUES (3, 'ada', FALSE)");

    EXPECT_EQ(exec(database, "UPDATE users SET name = 'ada lovelace' WHERE id = 1").message,
              "Updated 1 row.");
    EXPECT_EQ(exec(database, "UPDATE users SET active = TRUE").message, "Updated 3 rows.");
    EXPECT_EQ(exec(database, "UPDATE users SET name = 'nobody' WHERE id = 9").message,
              "Updated 0 rows.");
    EXPECT_EQ(rows_of(exec(database, "SELECT id, name, active FROM users")).rows,
              (std::vector<std::vector<std::string>>{
                  {"1", "ada lovelace", "TRUE"},
                  {"2", "grace", "TRUE"},
                  {"3", "ada", "TRUE"},
              }));

    EXPECT_EQ(exec(database, "DELETE FROM users WHERE name != 'ada'").message, "Deleted 2 rows.");
    EXPECT_EQ(rows_of(exec(database, "SELECT id FROM users")).rows,
              (std::vector<std::vector<std::string>>{{"3"}}));
    EXPECT_EQ(exec(database, "DELETE FROM users").message, "Deleted 1 row.");
    EXPECT_EQ(rows_of(exec(database, "SELECT * FROM users")).rows,
              std::vector<std::vector<std::string>>{});
    EXPECT_EQ(exec(database, "DELETE FROM users WHERE id = 1").message, "Deleted 0 rows.");
}

TEST(Executor, DropRemovesTheTableAndNamesAreCaseSensitive) {
    minidb::Database database;
    exec(database, "CREATE TABLE Users (Id INT)");
    exec(database, "CREATE TABLE users (id INT)");
    exec(database, "INSERT INTO Users VALUES (1)");
    exec(database, "INSERT INTO users VALUES (2)");
    EXPECT_EQ(database.table_names(), (std::vector<std::string>{"Users", "users"}));

    EXPECT_EQ(exec(database, "DROP TABLE Users").message, "Dropped table Users.");
    expect_error(database, "SELECT * FROM Users", "No such table: Users");
    EXPECT_EQ(rows_of(exec(database, "SELECT id FROM users")).rows,
              (std::vector<std::vector<std::string>>{{"2"}}));
    EXPECT_EQ(database.table_names(), std::vector<std::string>{"users"});

    exec(database, "CREATE TABLE Users (Id INT)");
    EXPECT_EQ(minidb::format_schema(database.require_table("Users")),
              "CREATE TABLE Users (Id INT);");
}

TEST(Executor, SelectKeepsTrailingSpacesInText) {
    minidb::Database database;
    exec(database, "CREATE TABLE t (note TEXT)");
    exec(database, "INSERT INTO t VALUES ('ada ')");
    EXPECT_EQ(rows_of(exec(database, "SELECT note FROM t")).rows,
              (std::vector<std::vector<std::string>>{{"ada "}}));
    EXPECT_EQ(minidb::format_result_set(rows_of(exec(database, "SELECT note FROM t"))),
              "note\n"
              "----\n"
              "ada \n"
              "(1 row)");
}

TEST(Executor, QuotedTextAndIntegerSpelling) {
    minidb::Database database;
    exec(database, "CREATE TABLE t (id INT, note TEXT)");
    exec(database, "INSERT INTO t VALUES (007, 'it''s')");
    EXPECT_EQ(rows_of(exec(database, "SELECT * FROM t")).rows,
              (std::vector<std::vector<std::string>>{{"7", "it's"}}));
    EXPECT_EQ(rows_of(exec(database, "SELECT note FROM t WHERE note = 'it''s'")).rows,
              (std::vector<std::vector<std::string>>{{"it's"}}));
}
