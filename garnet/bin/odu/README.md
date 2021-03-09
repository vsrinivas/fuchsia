# odu: A generic filesystem benchmarking utility for fuchsia

One of the things odu is used for is to run the following test cases:



  * sequential/write/8192: Measures the time to write blocks of size 8KiB
    in turn to a newly-created file, using the system filesystem, without any
    explicit syncing or flushing. The resulting metric is the time for each
    per-block write to return.
  * sequential/write/random_size: Measures the time to write blocks of
    random size less than 8KiB in turn to a newly-created file, using the
    system filesystem, without any explicit syncing or flushing. The resulting
    metric is the time for each per-block write to return.
