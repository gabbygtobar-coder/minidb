#include "minidb/catalog.hpp"
#include "minidb/execution_error.hpp"
#include "minidb/executor.hpp"
#include "minidb/pager.hpp"
#include "minidb/parser.hpp"
#include "minidb/storage_error.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace {

class TempPath {
  public:
    explicit TempPath(std::string path) : path_(std::move(path)) { std::remove(path_.c_str()); }

    ~TempPath() { std::remove(path_.c_str()); }

    TempPath(const TempPath&) = delete;
    TempPath& operator=(const TempPath&) = delete;

    const std::string& path() const { return path_; }

  private:
    std::string path_;
};

minidb::StatementResult exec(minidb::Database& database, const std::string& sql) {
    return minidb::execute(database, minidb::parse_statement(sql));
}

minidb::ResultSet rows_of(minidb::StatementResult outcome) {
    EXPECT_TRUE(outcome.result.has_value());
    if (!outcome.result.has_value()) {
        return {};
    }
    return std::move(*outcome.result);
}

std::uint64_t file_size(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    EXPECT_TRUE(in.good());
    const auto size = in.tellg();
    EXPECT_GE(size, 0);
    return static_cast<std::uint64_t>(size);
}

}  // namespace

TEST(Persistence, SchemaAndRowsSurviveDestroyAndReopen) {
    TempPath file("minidb-persist-basic.db");
    {
        minidb::Database database(file.path());
        EXPECT_EQ(exec(database, "CREATE TABLE users (id INT, name TEXT, active BOOLEAN, score FLOAT);")
                      .message,
                  "Created table users.");
        EXPECT_EQ(exec(database, "INSERT INTO users VALUES (1, 'ada', TRUE, 3.14);").message,
                  "Inserted 1 row.");
        EXPECT_EQ(exec(database, "INSERT INTO users VALUES (2, 'grace', FALSE, 10.0);").message,
                  "Inserted 1 row.");
        EXPECT_EQ(exec(database, "UPDATE users SET name = 'ada lovelace' WHERE id = 1;").message,
                  "Updated 1 row.");
        EXPECT_EQ(exec(database, "DELETE FROM users WHERE active = FALSE;").message, "Deleted 1 row.");
    }
    {
        minidb::Database database(file.path());
        EXPECT_EQ(database.table_names(), std::vector<std::string>{"users"});
        EXPECT_EQ(minidb::format_schema(database.require_table("users")),
                  "CREATE TABLE users (id INT, name TEXT, active BOOLEAN, score FLOAT);");
        EXPECT_EQ(database.row_count("users"), 1u);
        EXPECT_EQ(rows_of(exec(database, "SELECT id, name, active, score FROM users")).rows,
                  (std::vector<std::vector<std::string>>{{"1", "ada lovelace", "TRUE", "3.14"}}));
    }
}

TEST(Persistence, HeapScanKeepsOrderAcrossPages) {
    TempPath file("minidb-persist-pages.db");
    constexpr int kRows = 250;
    {
        minidb::Database database(file.path());
        exec(database, "CREATE TABLE nums (n INT)");
        for (int i = 1; i <= kRows; ++i) {
            exec(database, "INSERT INTO nums VALUES (" + std::to_string(i) + ")");
        }
        exec(database, "UPDATE nums SET n = 1000 WHERE n = 220");
        exec(database, "DELETE FROM nums WHERE n = 2");
    }
    {
        minidb::Pager pager = minidb::Pager::open_file(file.path());
        EXPECT_GE(pager.page_count(), 4u);
    }
    {
        minidb::Database database(file.path());
        const minidb::ResultSet result = rows_of(exec(database, "SELECT n FROM nums"));
        ASSERT_EQ(result.rows.size(), static_cast<std::size_t>(kRows - 1));
        EXPECT_EQ(result.rows.front(), std::vector<std::string>{"1"});
        EXPECT_EQ(result.rows[1], std::vector<std::string>{"3"});
        bool saw_updated = false;
        int previous = 0;
        for (const std::vector<std::string>& row : result.rows) {
            ASSERT_EQ(row.size(), 1u);
            const int value = std::stoi(row[0]);
            if (value == 1000) {
                saw_updated = true;
            } else {
                EXPECT_GT(value, previous);
                previous = value;
            }
        }
        EXPECT_TRUE(saw_updated);
        EXPECT_EQ(rows_of(exec(database, "SELECT n FROM nums WHERE n = 1000")).rows,
                  (std::vector<std::vector<std::string>>{{"1000"}}));
        EXPECT_EQ(result.rows.back(), std::vector<std::string>{"250"});
    }
}

TEST(Persistence, GrowingATextValueKeepsNeighbors) {
    TempPath file("minidb-persist-overflow.db");
    const std::string first(2000, 'a');
    const std::string second(2000, 'b');
    const std::string grown(3000, 'A');
    {
        minidb::Database database(file.path());
        exec(database, "CREATE TABLE notes (body TEXT)");
        exec(database, "INSERT INTO notes VALUES ('" + first + "')");
        exec(database, "INSERT INTO notes VALUES ('" + second + "')");
        exec(database, "UPDATE notes SET body = '" + grown + "' WHERE body = '" + first + "'");
        exec(database, "INSERT INTO notes VALUES ('z')");
        exec(database, "UPDATE notes SET body = 'shrunk' WHERE body = '" + grown + "'");
    }
    {
        minidb::Database database(file.path());
        EXPECT_EQ(rows_of(exec(database, "SELECT body FROM notes")).rows,
                  (std::vector<std::vector<std::string>>{{"shrunk"}, {second}, {"z"}}));
        EXPECT_EQ(exec(database, "DELETE FROM notes WHERE body = 'shrunk'").message, "Deleted 1 row.");
    }
    {
        minidb::Database database(file.path());
        EXPECT_EQ(rows_of(exec(database, "SELECT body FROM notes")).rows,
                  (std::vector<std::vector<std::string>>{{second}, {"z"}}));
    }
}

TEST(Persistence, DropDoesNotResurrectRows) {
    TempPath file("minidb-persist-drop.db");
    {
        minidb::Database database(file.path());
        exec(database, "CREATE TABLE secret (note TEXT)");
        exec(database, "INSERT INTO secret VALUES ('do not come back')");
        exec(database, "CREATE TABLE kept (id INT)");
        exec(database, "INSERT INTO kept VALUES (7)");
        exec(database, "DROP TABLE secret");
    }
    {
        minidb::Database database(file.path());
        EXPECT_EQ(database.table_names(), std::vector<std::string>{"kept"});
        exec(database, "CREATE TABLE secret (note TEXT)");
        exec(database, "INSERT INTO secret VALUES ('fresh')");
    }
    {
        minidb::Database database(file.path());
        EXPECT_EQ(database.table_names(), (std::vector<std::string>{"kept", "secret"}));
        EXPECT_EQ(rows_of(exec(database, "SELECT note FROM secret")).rows,
                  (std::vector<std::vector<std::string>>{{"fresh"}}));
        EXPECT_EQ(rows_of(exec(database, "SELECT id FROM kept")).rows,
                  (std::vector<std::vector<std::string>>{{"7"}}));
    }
}

TEST(Persistence, DroppedPagesAreReused) {
    TempPath file("minidb-persist-reuse.db");
    const auto fill = [](minidb::Database& database) {
        exec(database, "CREATE TABLE secret (note TEXT)");
        for (int i = 0; i < 300; ++i) {
            exec(database, "INSERT INTO secret VALUES ('" + std::string(40, 'x') + "')");
        }
    };
    {
        minidb::Database database(file.path());
        fill(database);
    }
    const std::uint64_t before = file_size(file.path());
    {
        minidb::Database database(file.path());
        exec(database, "DROP TABLE secret");
        fill(database);
    }
    EXPECT_EQ(file_size(file.path()), before);
    EXPECT_EQ(before % minidb::kPageSize, 0u);
}

TEST(Persistence, FailedInsertDoesNotChangeTheFile) {
    TempPath file("minidb-persist-reject.db");
    {
        minidb::Database database(file.path());
        exec(database, "CREATE TABLE users (id INT, name TEXT)");
        exec(database, "INSERT INTO users VALUES (1, 'ada')");
        try {
            exec(database, "INSERT INTO users VALUES ('no', 'ada')");
            FAIL() << "expected a type error";
        } catch (const minidb::ExecutionError&) {
        }
        const std::string big(5000, 'x');
        try {
            exec(database, "INSERT INTO users VALUES (2, '" + big + "')");
            FAIL() << "expected a page-size error";
        } catch (const minidb::ExecutionError& error) {
            EXPECT_NE(std::string(error.what()).find("does not fit in a single page"), std::string::npos);
        }
    }
    {
        minidb::Database database(file.path());
        EXPECT_EQ(rows_of(exec(database, "SELECT id, name FROM users")).rows,
                  (std::vector<std::vector<std::string>>{{"1", "ada"}}));
        exec(database, "INSERT INTO users VALUES (2, 'grace')");
    }
    {
        minidb::Database database(file.path());
        EXPECT_EQ(rows_of(exec(database, "SELECT name FROM users")).rows,
                  (std::vector<std::vector<std::string>>{{"ada"}, {"grace"}}));
    }
}

TEST(Persistence, NamesStayCaseSensitive) {
    TempPath file("minidb-persist-case.db");
    {
        minidb::Database database(file.path());
        exec(database, "CREATE TABLE Users (Id INT)");
        exec(database, "CREATE TABLE users (id INT)");
        exec(database, "INSERT INTO Users VALUES (1)");
        exec(database, "INSERT INTO users VALUES (2)");
    }
    {
        minidb::Database database(file.path());
        EXPECT_EQ(database.table_names(), (std::vector<std::string>{"Users", "users"}));
        EXPECT_EQ(minidb::format_schema(database.require_table("Users")), "CREATE TABLE Users (Id INT);");
        EXPECT_EQ(rows_of(exec(database, "SELECT Id FROM Users")).rows,
                  (std::vector<std::vector<std::string>>{{"1"}}));
        EXPECT_EQ(rows_of(exec(database, "SELECT id FROM users")).rows,
                  (std::vector<std::vector<std::string>>{{"2"}}));
    }
}

TEST(Persistence, EmptyFileAndQuotedText) {
    TempPath file("minidb-persist-empty.db");
    {
        std::ofstream out(file.path(), std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.good());
    }
    {
        minidb::Database database(file.path());
        exec(database, "CREATE TABLE t (note TEXT)");
        exec(database, "INSERT INTO t VALUES ('it''s')");
        exec(database, "INSERT INTO t VALUES ('ada ')");
    }
    {
        minidb::Database database(file.path());
        EXPECT_EQ(rows_of(exec(database, "SELECT note FROM t")).rows,
                  (std::vector<std::vector<std::string>>{{"it's"}, {"ada "}}));
    }
}
