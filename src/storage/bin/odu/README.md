# odu: A generic filesystem benchmarking utility for fuchsia

One of the things odu is used for is to run the following test cases:



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
