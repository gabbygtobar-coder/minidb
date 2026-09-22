#include "minidb/btree.hpp"
#include "minidb/catalog.hpp"
#include "minidb/execution_error.hpp"
#include "minidb/executor.hpp"
#include "minidb/parser.hpp"
#include "minidb/repl.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

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

std::vector<std::vector<std::string>> sorted_rows(minidb::ResultSet result) {
    std::sort(result.rows.begin(), result.rows.end());
    return result.rows;
}

std::string explain(minidb::Database& database, const std::string& sql) {
    return minidb::explain_statement(database, minidb::parse_statement(sql));
}

class TempPath {
  public:
    explicit TempPath(std::string path) : path_(std::move(path)) { std::remove(path_.c_str()); }
    ~TempPath() { std::remove(path_.c_str()); }
    const std::string& path() const { return path_; }

  private:
    std::string path_;
};

}  // namespace

TEST(Index, CreateInsertLookupUpdateDeleteAndDrop) {
    minidb::Database database;
    exec(database, "CREATE TABLE users (id INT, name TEXT, active BOOLEAN, score FLOAT)");
    EXPECT_EQ(explain(database, "SELECT * FROM users WHERE id = 1"),
              "Seq Scan on users\n  Filter: id = 1");

    EXPECT_EQ(exec(database, "CREATE INDEX idx_id ON users (id)").message,
              "Created index idx_id on users(id).");
    EXPECT_EQ(exec(database, "CREATE INDEX idx_name ON users (name)").message,
              "Created index idx_name on users(name).");
    EXPECT_EQ(exec(database, "CREATE INDEX idx_active ON users (active)").message,
              "Created index idx_active on users(active).");
    EXPECT_EQ(exec(database, "CREATE INDEX idx_score ON users (score)").message,
              "Created index idx_score on users(score).");
    EXPECT_EQ(database.index_names(),
              (std::vector<std::string>{"idx_active", "idx_id", "idx_name", "idx_score"}));

    exec(database, "INSERT INTO users VALUES (2, 'grace', FALSE, 10.0)");
    exec(database, "INSERT INTO users VALUES (1, 'ada', TRUE, 1.5)");
    exec(database, "INSERT INTO users VALUES (1, 'alan', TRUE, 1.5)");
    exec(database, "INSERT INTO users VALUES (3, 'linus', FALSE, 0.5)");

    EXPECT_EQ(explain(database, "SELECT * FROM users WHERE id = 1"),
              "Index Scan using idx_id on users\n  Index Cond: id = 1");
    EXPECT_EQ(explain(database, "SELECT * FROM users WHERE id != 1"),
              "Seq Scan on users\n  Filter: id != 1");
    EXPECT_EQ(explain(database, "SELECT * FROM users"), "Seq Scan on users");
    EXPECT_EQ(explain(database, "SELECT * FROM users WHERE id > 1"),
              "Index Scan using idx_id on users\n  Index Cond: id > 1");
    EXPECT_EQ(explain(database, "INSERT INTO users VALUES (4, 'kay', TRUE, 1.0)"),
              "No scan\n(CREATE, DROP, and INSERT do not scan a table)");

    const minidb::ResultSet by_id = rows_of(exec(database, "SELECT name FROM users WHERE id = 1"));
    EXPECT_EQ(sorted_rows(by_id), (std::vector<std::vector<std::string>>{{"ada"}, {"alan"}}));

    const minidb::ResultSet by_name = rows_of(exec(database, "SELECT id FROM users WHERE name = 'grace'"));
    ASSERT_EQ(by_name.rows.size(), 1u);
    EXPECT_EQ(by_name.rows[0], (std::vector<std::string>{"2"}));

    const minidb::ResultSet active = rows_of(exec(database, "SELECT name FROM users WHERE active = TRUE"));
    EXPECT_EQ(sorted_rows(active), (std::vector<std::vector<std::string>>{{"ada"}, {"alan"}}));

    const minidb::ResultSet high = rows_of(exec(database, "SELECT name FROM users WHERE score >= 1.5"));
    EXPECT_EQ(sorted_rows(high), (std::vector<std::vector<std::string>>{{"ada"}, {"alan"}, {"grace"}}));

    const minidb::ResultSet low = rows_of(exec(database, "SELECT name FROM users WHERE score < 1.5"));
    ASSERT_EQ(low.rows.size(), 1u);
    EXPECT_EQ(low.rows[0], (std::vector<std::string>{"linus"}));

    EXPECT_EQ(exec(database, "UPDATE users SET id = 9 WHERE name = 'ada'").message, "Updated 1 row.");
    ASSERT_EQ(rows_of(exec(database, "SELECT name FROM users WHERE id = 1")).rows.size(), 1u);
    const minidb::ResultSet moved = rows_of(exec(database, "SELECT name FROM users WHERE id = 9"));
    ASSERT_EQ(moved.rows.size(), 1u);
    EXPECT_EQ(moved.rows[0], (std::vector<std::string>{"ada"}));

    EXPECT_EQ(exec(database, "UPDATE users SET name = 'ada lovelace' WHERE id = 9").message, "Updated 1 row.");
    const minidb::ResultSet renamed = rows_of(exec(database, "SELECT name FROM users WHERE id = 9"));
    ASSERT_EQ(renamed.rows.size(), 1u);
    EXPECT_EQ(renamed.rows[0], (std::vector<std::string>{"ada lovelace"}));
    EXPECT_TRUE(rows_of(exec(database, "SELECT id FROM users WHERE name = 'ada'")).rows.empty());
    const minidb::ResultSet new_name =
        rows_of(exec(database, "SELECT id FROM users WHERE name = 'ada lovelace'"));
    ASSERT_EQ(new_name.rows.size(), 1u);
    EXPECT_EQ(new_name.rows[0], (std::vector<std::string>{"9"}));

    EXPECT_EQ(exec(database, "DELETE FROM users WHERE id = 9").message, "Deleted 1 row.");
    EXPECT_TRUE(rows_of(exec(database, "SELECT * FROM users WHERE id = 9")).rows.empty());
    // id 9 was just deleted, so only alan (1.5) and linus (0.5) remain below 10.
    EXPECT_EQ(exec(database, "DELETE FROM users WHERE score < 10.0").message, "Deleted 2 rows.");
    const minidb::ResultSet left = rows_of(exec(database, "SELECT name FROM users WHERE score >= 0.0"));
    ASSERT_EQ(left.rows.size(), 1u);
    EXPECT_EQ(left.rows[0], (std::vector<std::string>{"grace"}));

    EXPECT_EQ(exec(database, "DROP INDEX idx_id").message, "Dropped index idx_id.");
    EXPECT_EQ(explain(database, "SELECT * FROM users WHERE id = 2"),
              "Seq Scan on users\n  Filter: id = 2");
    const minidb::ResultSet after_drop = rows_of(exec(database, "SELECT name FROM users WHERE id = 2"));
    ASSERT_EQ(after_drop.rows.size(), 1u);
    EXPECT_EQ(after_drop.rows[0], (std::vector<std::string>{"grace"}));

    exec(database, "DROP TABLE users");
    EXPECT_TRUE(database.index_names().empty());
    EXPECT_THROW(exec(database, "DROP INDEX idx_name"), minidb::ExecutionError);
}

TEST(Index, MatchesHeapScanForEqualityAndRange) {
    minidb::Database database;
    exec(database, "CREATE TABLE t (id INT, tag TEXT)");
    for (int id = 0; id < 80; ++id) {
        exec(database, "INSERT INTO t VALUES (" + std::to_string(id % 7) + ", 'v" + std::to_string(id) + "')");
    }
    exec(database, "CREATE INDEX idx ON t (id)");

    const std::vector<std::string> queries = {
        "SELECT tag FROM t WHERE id = 3",
        "SELECT tag FROM t WHERE id < 2",
        "SELECT tag FROM t WHERE id <= 2",
        "SELECT tag FROM t WHERE id > 5",
        "SELECT tag FROM t WHERE id >= 5",
        "SELECT tag FROM t WHERE id != 3",
    };
    for (const std::string& sql : queries) {
        const std::vector<std::vector<std::string>> indexed = sorted_rows(rows_of(exec(database, sql)));
        exec(database, "DROP INDEX idx");
        const std::vector<std::vector<std::string>> scanned = sorted_rows(rows_of(exec(database, sql)));
        EXPECT_EQ(indexed, scanned) << sql;
        exec(database, "CREATE INDEX idx ON t (id)");
    }
}

TEST(Index, ReopenKeepsTheIndex) {
    TempPath file("minidb-index-reopen.db");
    {
        minidb::Database database(file.path());
        exec(database, "CREATE TABLE users (id INT, name TEXT)");
        exec(database, "CREATE INDEX idx ON users (id)");
        exec(database, "INSERT INTO users VALUES (1, 'ada')");
        exec(database, "INSERT INTO users VALUES (2, 'grace')");
        exec(database, "INSERT INTO users VALUES (3, 'linus')");
    }
    {
        minidb::Database database(file.path());
        EXPECT_EQ(database.index_names(), std::vector<std::string>{"idx"});
        EXPECT_EQ(explain(database, "SELECT name FROM users WHERE id = 2"),
                  "Index Scan using idx on users\n  Index Cond: id = 2");
        const minidb::ResultSet result = rows_of(exec(database, "SELECT name FROM users WHERE id = 2"));
        ASSERT_EQ(result.rows.size(), 1u);
        EXPECT_EQ(result.rows[0], (std::vector<std::string>{"grace"}));
        const minidb::ResultSet range = rows_of(exec(database, "SELECT name FROM users WHERE id >= 2"));
        EXPECT_EQ(sorted_rows(range), (std::vector<std::vector<std::string>>{{"grace"}, {"linus"}}));
    }
}

TEST(Index, PointLookupReadsFewerPagesThanAScan) {
    minidb::Database database;
    exec(database, "CREATE TABLE t (id INT, name TEXT)");
    constexpr int kRows = 500;
    for (int id = 0; id < kRows; ++id) {
        exec(database, "INSERT INTO t VALUES (" + std::to_string(id) + ", 'row')");
    }

    database.reset_page_reads();
    const minidb::ResultSet scanned = rows_of(exec(database, "SELECT id FROM t WHERE id = 250"));
    const std::uint64_t scan_reads = database.page_reads();
    ASSERT_EQ(scanned.rows.size(), 1u);
    EXPECT_EQ(scanned.rows[0], (std::vector<std::string>{"250"}));

    exec(database, "CREATE INDEX idx ON t (id)");
    database.reset_page_reads();
    const minidb::ResultSet indexed = rows_of(exec(database, "SELECT id FROM t WHERE id = 250"));
    const std::uint64_t index_reads = database.page_reads();
    ASSERT_EQ(indexed.rows.size(), 1u);
    EXPECT_EQ(indexed.rows[0], (std::vector<std::string>{"250"}));

    EXPECT_GT(scan_reads, 100u);
    EXPECT_LT(index_reads, scan_reads) << "seq scan reads=" << scan_reads
                                       << " index reads=" << index_reads;
}

TEST(Index, RejectsUnknownNamesAndOversizedKeys) {
    minidb::Database database;
    exec(database, "CREATE TABLE t (id INT, note TEXT)");
    EXPECT_THROW(exec(database, "CREATE INDEX idx ON missing (id)"), minidb::ExecutionError);
    EXPECT_THROW(exec(database, "CREATE INDEX idx ON t (missing)"), minidb::ExecutionError);
    exec(database, "CREATE INDEX idx ON t (note)");
    EXPECT_THROW(exec(database, "CREATE INDEX idx ON t (id)"), minidb::ExecutionError);

    const std::string huge(minidb::kMaxIndexKeyBytes + 1, 'a');
    EXPECT_THROW(exec(database, "INSERT INTO t VALUES (1, '" + huge + "')"), minidb::ExecutionError);
    EXPECT_EQ(database.row_count("t"), 0u);

    exec(database, "DROP INDEX idx");
    exec(database, "INSERT INTO t VALUES (1, '" + huge + "')");
    EXPECT_THROW(exec(database, "CREATE INDEX idx ON t (note)"), minidb::ExecutionError);
    EXPECT_TRUE(database.index_names().empty());
    EXPECT_EQ(rows_of(exec(database, "SELECT id FROM t")).rows.size(), 1u);
}

TEST(Index, ExplainPrintsThePredicateAndStatesItsLimits) {
    minidb::Database database;
    exec(database, "CREATE TABLE users (id INT, name TEXT, active BOOLEAN, score FLOAT)");
    EXPECT_EQ(explain(database, "SELECT name FROM users WHERE id = 007"),
              "Seq Scan on users\n  Filter: id = 007");
    EXPECT_EQ(explain(database, "SELECT id FROM users WHERE name = 'it''s'"),
              "Seq Scan on users\n  Filter: name = 'it''s'");
    EXPECT_EQ(explain(database, "SELECT id FROM users WHERE active = FALSE"),
              "Seq Scan on users\n  Filter: active = FALSE");

    exec(database, "CREATE INDEX idx_id ON users (id)");
    exec(database, "CREATE INDEX idx_score ON users (score)");
    EXPECT_EQ(explain(database, "SELECT * FROM users WHERE score >= 1.50"),
              "Index Scan using idx_score on users\n  Index Cond: score >= 1.50");
    EXPECT_EQ(explain(database, "UPDATE users SET name = 'a' WHERE id = 1"),
              "Index Scan using idx_id on users\n  Index Cond: id = 1\n"
              "(read plan only; EXPLAIN does not describe the write)");
    EXPECT_EQ(explain(database, "DELETE FROM users WHERE active = TRUE"),
              "Seq Scan on users\n  Filter: active = TRUE\n"
              "(read plan only; EXPLAIN does not describe the write)");
    EXPECT_EQ(explain(database, "DELETE FROM users"),
              "No scan\n(DELETE without WHERE clears the heap and does not walk rows)");
    EXPECT_EQ(explain(database, "CREATE INDEX idx_name ON users (name)"),
              "No scan\n(CREATE, DROP, and INSERT do not scan a table)");
    EXPECT_EQ(explain(database, "DROP TABLE users"),
              "No scan\n(CREATE, DROP, and INSERT do not scan a table)");
}

TEST(Index, ExplainMetaCommandDoesNotRunTheStatement) {
    std::istringstream in(
        "CREATE TABLE t (id INT);\n"
        "INSERT INTO t VALUES (1);\n"
        ".explain SELECT * FROM t WHERE id = 1\n"
        "CREATE INDEX idx ON t (id);\n"
        ".explain SELECT * FROM t WHERE id = 1\n"
        "SELECT * FROM t WHERE id = 1;\n"
        ".explain\n"
        ".exit\n");
    std::ostringstream out;
    minidb::Repl repl(in, out);
    EXPECT_EQ(repl.run(), 0);
    const std::string text = out.str();
    EXPECT_NE(text.find("Seq Scan on t\n"), std::string::npos);
    EXPECT_NE(text.find("Index Scan using idx on t\n"), std::string::npos);
    EXPECT_NE(text.find("Usage: .explain <sql>\n"), std::string::npos);
    EXPECT_NE(text.find("(1 row)"), std::string::npos);
}
