#include "minidb/repl.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <utility>

namespace {

std::string run_session(const std::string& input, minidb::ReplOptions options = {}) {
    std::istringstream in(input);
    std::ostringstream out;
    minidb::Repl repl(in, out, std::move(options));
    EXPECT_EQ(repl.run(), 0);
    return out.str();
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST(Repl, BannerShowsVersionAndDefaultDatabase) {
    const std::string output = run_session(".exit\n");
    EXPECT_TRUE(contains(output, "MiniDB v0.1\n"));
    EXPECT_TRUE(contains(output, "Database: local\n"));
    EXPECT_TRUE(contains(output, "MiniDB> "));
}

TEST(Repl, BannerShowsRequestedDataDirectory) {
    minidb::ReplOptions options;
    options.database = "/tmp/minidb-data";
    const std::string output = run_session(".quit\n", options);
    EXPECT_TRUE(contains(output, "Database: /tmp/minidb-data\n"));
}

TEST(Repl, HelpListsMetaCommands) {
    const std::string output = run_session(".help\n.exit\n");
    EXPECT_TRUE(contains(output, ".help"));
    EXPECT_TRUE(contains(output, ".tables"));
    EXPECT_TRUE(contains(output, ".schema"));
    EXPECT_TRUE(contains(output, ".exit"));
    EXPECT_TRUE(contains(output, ".quit"));
    EXPECT_TRUE(contains(output, "Data is lost when the shell exits."));
}

TEST(Repl, ExecutesSqlAgainstMemory) {
    const std::string output = run_session(
        "CREATE TABLE users (id INT, name TEXT);\n"
        "INSERT INTO users VALUES (1, 'ada');\n"
        "  SELECT * FROM users WHERE id = 1  \n"
        ".exit\n");
    EXPECT_TRUE(contains(output, "Created table users.\n"));
    EXPECT_TRUE(contains(output, "Inserted 1 row.\n"));
    EXPECT_TRUE(contains(output, "1  | ada\n"));
    EXPECT_TRUE(contains(output, "(1 row)\n"));
    EXPECT_FALSE(contains(output, "Parsed:"));
}

TEST(Repl, ReportsParseErrorsWithoutExecuting) {
    const std::string output = run_session("SELECT 1;\n.exit\n");
    EXPECT_TRUE(contains(output, "Parse error at 1:8: expected '*' or a column name, found integer '1'\n"));
    EXPECT_FALSE(contains(output, "Parsed:"));
}

TEST(Repl, UnknownMetaCommandIsRejected) {
    const std::string output = run_session(".foo\n.exit\n");
    EXPECT_TRUE(contains(output, "Unknown meta-command: .foo\n"));
    EXPECT_FALSE(contains(output, "Parsed:"));
    EXPECT_FALSE(contains(output, "Parse error"));
}

TEST(Repl, EmptyLinesDoNotCrashOrReject) {
    const std::string output = run_session("\n   \n\t\n.exit\n");
    EXPECT_FALSE(contains(output, "Parse error"));
    EXPECT_FALSE(contains(output, "Parsed:"));
    EXPECT_TRUE(contains(output, "MiniDB> "));
}

TEST(Repl, WhitespaceAroundMetaCommandsIsIgnored) {
    const std::string output = run_session("  .help  \n  .exit  \n");
    EXPECT_TRUE(contains(output, "MiniDB meta-commands:"));
    EXPECT_TRUE(contains(output, "Data is lost when the shell exits."));
    EXPECT_FALSE(contains(output, "Parse error"));
}

TEST(Repl, QuitStopsBeforeLaterInput) {
    const std::string output = run_session(".quit\nSELECT 1;\n");
    EXPECT_FALSE(contains(output, "Parse error"));
    EXPECT_FALSE(contains(output, "Parsed:"));
}

TEST(Repl, TablesSchemaAndExecutionErrors) {
    const std::string output = run_session(
        ".tables\n"
        "CREATE TABLE users (id INT, name TEXT);\n"
        "CREATE TABLE users (id INT);\n"
        "INSERT INTO users VALUES (1);\n"
        "INSERT INTO users VALUES (1, 'x');\n"
        "INSERT INTO missing VALUES (1);\n"
        ".tables\n"
        ".schema users\n"
        ".schema\n"
        ".schema missing\n"
        ".schemax\n"
        "UPDATE users SET name = 'ada' WHERE id = 1;\n"
        "DELETE FROM users WHERE id = 1;\n"
        "SELECT * FROM users;\n"
        "DROP TABLE users;\n"
        ".tables\n"
        ".exit\n");
    EXPECT_TRUE(contains(output, "(no tables)\n"));
    EXPECT_TRUE(contains(output, "Error: Table already exists: users\n"));
    EXPECT_TRUE(contains(output, "Error: INSERT INTO users expected 2 values, got 1\n"));
    EXPECT_TRUE(contains(output, "Error: No such table: missing\n"));
    EXPECT_TRUE(contains(output, "users\n"));
    EXPECT_TRUE(contains(output, "CREATE TABLE users (id INT, name TEXT);\n"));
    EXPECT_TRUE(contains(output, "Usage: .schema <table>\n"));
    EXPECT_TRUE(contains(output, "Unknown meta-command: .schemax\n"));
    EXPECT_TRUE(contains(output, "Updated 1 row.\n"));
    EXPECT_TRUE(contains(output, "Deleted 1 row.\n"));
    EXPECT_TRUE(contains(output, "(0 rows)\n"));
    EXPECT_TRUE(contains(output, "Dropped table users.\n"));
    EXPECT_FALSE(contains(output, "Parse error"));
}

TEST(Repl, EachSessionStartsEmpty) {
    const std::string first = run_session("CREATE TABLE users (id INT);\n.tables\n.exit\n");
    EXPECT_TRUE(contains(first, "users\n"));
    const std::string second = run_session(".tables\n.exit\n");
    EXPECT_TRUE(contains(second, "(no tables)\n"));
    EXPECT_FALSE(contains(second, "users"));
}

TEST(Repl, EndOfInputExitsCleanly) {
    const std::string output = run_session("");
    EXPECT_TRUE(contains(output, "MiniDB v0.1\n"));
    EXPECT_TRUE(contains(output, "Database: local\n"));
    EXPECT_TRUE(contains(output, "MiniDB> \n"));
    EXPECT_FALSE(contains(output, "Parse error"));
    EXPECT_FALSE(contains(output, "Parsed:"));
}
