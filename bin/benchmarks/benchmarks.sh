#!/boot/bin/sh
#
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# This script runs all benchmarks for the Garnet layer.
#
# Usage: benchmarks.sh <output-dir>
# Example: benchmarks.sh /tmp

# Import the runbenchmarks library.
. /pkgfs/packages/runbenchmarks/0/data/runbenchmarks.sh

# Ensure the output directory is specified.
if [ $# -ne 1 ]; then
    echo "error: missing output directory"
    echo "Usage: $0 <output-dir>"
    exit 1
fi
OUT_DIR="$1"

# Benchmark example, here for demonstration.
# TODO(IN-458): Re-enable after tracing failure is addressed.
#runbench_exec "${OUT_DIR}/benchmark_example.json" \
#    trace record \
#    --spec-file=/system/data/benchmark_example/benchmark_example.tspec \
#    --benchmark-results-file="${OUT_DIR}/benchmark_example.json"

# Performance tests implemented in the Zircon repo.
runbench_exec "${OUT_DIR}/zircon.perf_test.json" \
    /system/test/sys/perf-test -p --out="${OUT_DIR}/zircon.perf_test.json"

# Performance tests implemented in the Garnet repo (the name
# "zircon_benchmarks" is now misleading).
runbench_exec "${OUT_DIR}/zircon_benchmarks.json" \
    /pkgfs/packages/zircon_benchmarks/0/bin/app -p --out="${OUT_DIR}/zircon_benchmarks.json"

# Scenic performance tests.
runbench_exec "${OUT_DIR}/benchmark_hello_scenic.json" \
    /system/data/scenic_benchmarks/hello_scenic_benchmark.sh "${OUT_DIR}" "${OUT_DIR}/benchmark_hello_scenic.json"
# Exit with a code indicating whether any errors occurred.
runbench_finish "${OUT_DIR}"
