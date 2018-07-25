#!/boot/bin/sh
#
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# This script runs all benchmarks for the Garnet layer.
#
# For usage, see runbench_read_arguments in runbenchmarks.sh.

# Import the runbenchmarks library.
. /pkgfs/packages/runbenchmarks/0/data/runbenchmarks.sh

runbench_read_arguments "$@"

# Benchmark example, here for demonstration.
runbench_exec "${OUT_DIR}/benchmark_example.json" \
    trace record \
    --spec-file=/system/data/benchmark_example/benchmark_example.tspec \
    --test-suite=fuchsia.benchmark_example \
    --benchmark-results-file="${OUT_DIR}/benchmark_example.json"

# Performance tests implemented in the Zircon repo.
runbench_exec "${OUT_DIR}/zircon.perf_test.json" \
    /system/test/sys/perf-test -p --out="${OUT_DIR}/zircon.perf_test.json"

# Performance tests implemented in the Garnet repo (the name
# "zircon_benchmarks" is now misleading).
runbench_exec "${OUT_DIR}/zircon_benchmarks.json" \
    /pkgfs/packages/zircon_benchmarks/0/bin/app -p --out="${OUT_DIR}/zircon_benchmarks.json"

# Scenic performance tests.
# TODO(SCN-832): Re-enable when these tests pass on the perf bots.
#runbench_exec "${OUT_DIR}/benchmark_hello_scenic.json" \
#    /pkgfs/packages/scenic_benchmarks/0/data/hello_scenic_benchmark.sh "${OUT_DIR}" "${OUT_DIR}/benchmark_hello_scenic.json"

# Exit with a code indicating whether any errors occurred.
runbench_finish "${OUT_DIR}"
