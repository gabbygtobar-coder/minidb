# Benchmarks

No timing harness yet.

M4 can avoid a heap scan when a B+ tree exists for the filtered column. The regression test `Index.PointLookupReadsFewerPagesThanAScan` counts logical `read_page` calls, including cache hits, for `SELECT id FROM t WHERE id = 250` over 500 rows. On a Debug build of this tree that was 504 reads without an index and 5 reads with one. The scan count includes a second read of each heap page for every live row. It is not a disk-I/O or a wall-clock measurement. Add timings here once a number would say something that test does not.
