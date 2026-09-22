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
    EXPECT_TRUE(contains(output, ".exit"));
    EXPECT_TRUE(contains(output, ".quit"));
}

TEST(Repl, ParsesSqlAndPrintsAst) {
    const std::string output = run_session(
        "CREATE TABLE users (id INT, name TEXT);\n"
        "  SELECT * FROM users WHERE id = 1  \n"
        ".exit\n");
    EXPECT_TRUE(contains(output, "Parsed: CreateTable users (id INT, name TEXT)\n"));
    EXPECT_TRUE(contains(output, "Parsed: Select * FROM users WHERE id = 1\n"));
    EXPECT_FALSE(contains(output, "rows"));
    EXPECT_FALSE(contains(output, "SQL engine is not implemented"));
}

TEST(Repl, ReportsParseErrorsWithoutExecuting) {
    const std::string output = run_session("SELECT 1;\n.exit\n");
    EXPECT_TRUE(contains(output, "Parse error at 1:8: expected '*' or a column name, found integer '1'\n"));
    EXPECT_FALSE(contains(output, "Parsed:"));
}

TEST(Repl, UnknownMetaCommandIsRejected) {
    const std::string output = run_session(".tables\n.exit\n");
    EXPECT_TRUE(contains(output, "Unknown meta-command: .tables\n"));
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
    EXPECT_TRUE(contains(output, "They are not executed."));
    EXPECT_FALSE(contains(output, "Parse error"));
}

TEST(Repl, QuitStopsBeforeLaterInput) {
    const std::string output = run_session(".quit\nSELECT 1;\n");
    EXPECT_FALSE(contains(output, "Parse error"));
    EXPECT_FALSE(contains(output, "Parsed:"));
}

TEST(Repl, EndOfInputExitsCleanly) {
    const std::string output = run_session("");
    EXPECT_TRUE(contains(output, "MiniDB v0.1\n"));
    EXPECT_TRUE(contains(output, "Database: local\n"));
    EXPECT_TRUE(contains(output, "MiniDB> \n"));
    EXPECT_FALSE(contains(output, "Parse error"));
    EXPECT_FALSE(contains(output, "Parsed:"));
}
