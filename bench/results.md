# Point lookup

One measurement from `bash bench/run_point_lookup.sh`. Re-running the script prints a new sample and does not overwrite this file. These numbers are that sample. They are not a target and not a median of several runs.

## Machine

Recorded 2026-09-22T04:34:49Z on a cloud VM.

| | |
| --- | --- |
| `uname -srm` | Linux 6.12.94+ x86_64 |
| Compiler | Ubuntu clang version 18.1.3 (1ubuntu1) |
| `/proc/cpuinfo` model name | Intel(R) Xeon(R) Processor |
| CPUs | 4, reported at 2400 MHz |
| `MemTotal` | 16398384 kB |
| CMake build | Release (`-O3 -DNDEBUG`), `BUILD_TESTING=OFF` |

The model-name string is what the kernel reported. It does not name a specific Xeon SKU.

## Reproduce

```bash
bash bench/run_point_lookup.sh
```

The script configures `build-bench/` as Release, builds `minidb_bench`, prints the machine line, and runs the lookup. Source is `bench/point_lookup.cpp`.

## What it ran

- In-memory database (no file, so this run does not time `fflush` or disk).
- `CREATE TABLE t (id INT, name TEXT)`.
- 10,000 inserts, `id` from 0 through 9999, name `'row'`.
- Point lookup `SELECT id FROM t WHERE id = 5000`, which returns one row.
- The same statement again after `CREATE INDEX idx_id ON t (id)`.
- Logical page reads: one execution, `Database::page_reads`, which counts every `Pager::read_page` call, including pages already in the process cache.
- Wall time: 1,000 further executions of that same statement on the warm cache. The timed loop is only `execute`. It is not disk I/O.

With 1,000 repeats, total milliseconds and mean microseconds are the same quantity (`ms × 1000 / 1000`).

## Output

```text
machine: Linux 6.12.94+ x86_64
compiler: Ubuntu clang version 18.1.3 (1ubuntu1)
cpu: Intel(R) Xeon(R) Processor
date_utc: 2026-09-22T04:34:49Z
minidb point lookup
rows: 10000
schema: t(id INT, name TEXT)
lookup: SELECT id FROM t WHERE id = 5000
database: memory
build: Release
repeats: 1000
load_inserts_wall_ms: 7.909
create_index_wall_ms: 127.887
plan_without_index: Seq Scan on t
  Filter: id = 5000
plan_with_index: Index Scan using idx_id on t
  Index Cond: id = 5000
seq_scan_logical_page_reads: 10067
index_scan_logical_page_reads: 5
seq_scan_warm_wall_ms: 2180.865
index_scan_warm_wall_ms: 10.621
seq_scan_mean_us: 2180.865
index_scan_mean_us: 10.621
logical_reads_include_cache_hits: yes
wall_time_is_disk_io: no
```

## How to read it

| | Sequential scan | Index scan |
| --- | --- | --- |
| Plan | `Seq Scan on t` / `Filter: id = 5000` | `Index Scan using idx_id on t` / `Index Cond: id = 5000` |
| Logical page reads, one lookup | 10067 | 5 |
| Warm wall time, 1000 lookups | 2180.865 ms | 10.621 ms |
| Mean per lookup | 2180.865 µs | 10.621 µs |

The sequential counter is large because the heap walker reads a page and then reads that page again for every live row. 10067 − 10000 = 67, which matches that code if every row is a normal record and the lookup does not read anything else. That subtraction is an interpretation of this counter, not a separate page-count measurement.

The index lookup reported 5 logical reads. It does not walk the other heap pages. This harness does not attribute those 5 to particular nodes.

Setup on the same run, not the comparison: inserting 10,000 rows took 7.909 ms, and `CREATE INDEX` took 127.887 ms.

Pages that have been read stay in memory. A second lookup does not go to disk. There is no `fsync` in this path. Do not quote these times as storage-device latency.
