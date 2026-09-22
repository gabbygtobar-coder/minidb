# Benchmarks

The measured point lookup lives in [bench/results.md](../bench/results.md). Reproduce it with `bash bench/run_point_lookup.sh`. That file is one Release run on a cloud VM: 10,000 rows, `SELECT id FROM t WHERE id = 5000`, logical page reads and warm-cache wall time, with and without an index. It is not a disk-I/O number.

`Index.PointLookupReadsFewerPagesThanAScan` is a regression test over 500 rows. It checks that the index path reads fewer pages than the scan. It does not record a wall time.
