# odu: A generic filesystem benchmarking utility for fuchsia

The tool can be built for host as well as for target by adding
`--with --with //src/storage/bin/odu,//src/storage/bin/odu:odu_host` (the host side binary can be
found in `out/<target>/host_x64/odu`).

More detailed docs can be generated with

```bash
fx gen-cargo //src/storage/bin/odu:bin
fx rustdoc --doc-private //src/storage/bin/odu:bin
```
## Sample commands

```bash
# Measure `read` performance on an a existing file at `/tmp/x`
odu --target /tmp/x --operations read

# measure sequential write performance using 400 IOs per thread with 8192 as block size alignment
odu --target /tmp/x --operations write --max_io_count=400 --sequential true --block_size 8192 --max_io_size 8192 --align true
```

## Use cases
The tool can be used to measure and compare performance between fuchsia and other POSIX-like OSes.

Other than manual runs, odu is used by bots to run the following test cases to measure perf:

  * sequential/write/8192: Measures the time to write blocks of size 8KiB
    in turn to a newly-created file, using the system filesystem, without any
    explicit syncing or flushing. The resulting metric is the time taken for
    each per-block write to return.
  * sequential/write/random_size: Measures the time to write blocks of
    random size less than 8KiB in turn to a newly-created file, using the
    system filesystem, without any explicit syncing or flushing. The resulting
    metric is the time taken for each per-block write to return.
  * sequential/read/8192: Measures the time to read blocks of size 8KiB
    in turn from a file, using the system filesystem. The resulting metrics
    is the time taken for each per-block read to return.
  * sequential/read/random_size: Measures the time to read blocks of random
    size less than 8KiB in turn from a file, using the system filesystem. The
    resulting metric is the time taken for each per-block read to return.
