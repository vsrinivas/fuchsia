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

# Run benchmarks.
runbench_trace "${OUT_DIR}/benchmark_example" /system/data/benchmark_example/benchmark_example.tspec
runbench_exec  "${OUT_DIR}/zircon_benchmarks" /system/test/perf/run_zircon_benchmarks

# Exit with a code indicating whether any errors occurred.
runbench_exit

