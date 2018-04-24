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
runbench_exec "${OUT_DIR}/benchmark_example" \
    trace record \
    --spec-file=/system/data/benchmark_example/benchmark_example.tspec \
    --benchmark-results-file="${OUT_DIR}/benchmark_example"

# Zircon benchmarks.
runbench_exec "${OUT_DIR}/zircon_benchmarks" \
    /system/bin/zircon_benchmarks -p --out="${OUT_DIR}/zircon_benchmarks"

# Exit with a code indicating whether any errors occurred.
runbench_finish "${OUT_DIR}"
