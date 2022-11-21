# User Copy Benchmark

This program is meant to measure the latency of performing copy operations with specific instruction set on arm64 platforms.

# How to run:

If your arm64 platform is able to be served by a PM server (a.k.a. fx serve), then adding `//zircon/kernel/arch/arm64/user-copy:benchmarks`
will be enough. If this is not the case, then adding `//zircon/kernel/arch/arm64/user-copy:benchmarks-standalone` to your base labels
is enough.

Once you have built, then the following binaries will be available on your fuchsia instance:

 * _arm64_user_copy : stp, ldp bidirectional
 * _arm64_user_copy_to_user :  ldp, sttr unidirectional
 * _arm64_user_copy_from_user : ldtr, stp unidirectional

The three binaries share the same set of command line arguments:
 * '--cpu, -c' cpu id to bind to. (fuchsia specific).
 * '--name,-n' name of the cpu(e.g. cortex-A53).
 * '--output,-o' path to an output file where to write the csv contents.
 * '--seed,-s' random seed to fix the copied contents (reproduceability).
 * '--profile,-p' allows setting up a deadline profile, which in theory should reduce variance
    (fuchsia specific).
 * '--logtostdout,-l' instead of writing output to a file, will write straight into stdout.

# Sampling

The sampling is performed as follows, for each block size, source alignment and destination alignment triplet a warm up is performed, which
consist of an arbitrary round of copies of arbitrary values into the range.

To reduce variance and noise, the measurement is performed of `kSampleCount` copies in sequence which is then averaged (divided by `kSampleCount`).

