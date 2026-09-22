#include "minidb/catalog.hpp"
#include "minidb/executor.hpp"
#include "minidb/parser.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

// Measures one equality lookup over a heap of kRows, then the same lookup
// after CREATE INDEX. Logical page reads count Pager::read_page calls,
// including pages already in the process cache. Wall time is a warm cache
// inside this process. Neither number is disk I/O, and neither is an fsync.

namespace {

constexpr int kRows = 10000;
constexpr int kLookupId = 5000;
constexpr int kRepeats = 1000;

#ifndef MINIDB_BENCH_CONFIG
#define MINIDB_BENCH_CONFIG "unspecified"
#endif

minidb::StatementResult exec(minidb::Database& database, const std::string& sql) {
    return minidb::execute(database, minidb::parse_statement(sql));
}

void expect_one_row(const minidb::StatementResult& outcome) {
    if (!outcome.result.has_value() || outcome.result->rows.size() != 1 ||
        outcome.result->rows[0].size() != 1 ||
        outcome.result->rows[0][0] != std::to_string(kLookupId)) {
        throw std::runtime_error("lookup did not return the requested id");
    }
}

std::uint64_t lookup_reads(minidb::Database& database, const std::string& sql) {
    database.reset_page_reads();
    const minidb::StatementResult outcome = exec(database, sql);
    expect_one_row(outcome);
    return database.page_reads();
}

// The untimed call checks the row. The timed loop is only execute(), so the
// number is the warm-cache cost of that statement, not the check around it.
double timed_repeats_ms(minidb::Database& database, const std::string& sql) {
    expect_one_row(exec(database, sql));
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kRepeats; ++i) {
        exec(database, sql);
    }
    const auto stop = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(stop - start).count();
}

}  // namespace

int main() {
    try {
        minidb::Database database;
        const auto load_start = std::chrono::steady_clock::now();
        exec(database, "CREATE TABLE t (id INT, name TEXT)");
        for (int id = 0; id < kRows; ++id) {
            exec(database, "INSERT INTO t VALUES (" + std::to_string(id) + ", 'row')");
        }
        const double load_ms = std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - load_start)
                                   .count();

        const std::string sql =
            "SELECT id FROM t WHERE id = " + std::to_string(kLookupId);
        const std::string plan_before = minidb::explain_statement(database, minidb::parse_statement(sql));
        const std::uint64_t seq_reads = lookup_reads(database, sql);
        const double seq_ms = timed_repeats_ms(database, sql);

        const auto index_start = std::chrono::steady_clock::now();
        exec(database, "CREATE INDEX idx_id ON t (id)");
        const double index_build_ms = std::chrono::duration<double, std::milli>(
                                           std::chrono::steady_clock::now() - index_start)
                                           .count();
        const std::string plan_after = minidb::explain_statement(database, minidb::parse_statement(sql));
        const std::uint64_t index_reads = lookup_reads(database, sql);
        const double index_ms = timed_repeats_ms(database, sql);

        std::cout.setf(std::ios::fixed);
        std::cout.precision(3);
        std::cout << "minidb point lookup\n"
                  << "rows: " << kRows << "\n"
                  << "schema: t(id INT, name TEXT)\n"
                  << "lookup: " << sql << "\n"
                  << "database: memory\n"
                  << "build: " << MINIDB_BENCH_CONFIG << "\n"
                  << "repeats: " << kRepeats << "\n"
                  << "load_inserts_wall_ms: " << load_ms << "\n"
                  << "create_index_wall_ms: " << index_build_ms << "\n"
                  << "plan_without_index: " << plan_before << "\n"
                  << "plan_with_index: " << plan_after << "\n"
                  << "seq_scan_logical_page_reads: " << seq_reads << "\n"
                  << "index_scan_logical_page_reads: " << index_reads << "\n"
                  << "seq_scan_warm_wall_ms: " << seq_ms << "\n"
                  << "index_scan_warm_wall_ms: " << index_ms << "\n"
                  << "seq_scan_mean_us: " << (seq_ms * 1000.0 / kRepeats) << "\n"
                  << "index_scan_mean_us: " << (index_ms * 1000.0 / kRepeats) << "\n"
                  << "logical_reads_include_cache_hits: yes\n"
                  << "wall_time_is_disk_io: no\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
