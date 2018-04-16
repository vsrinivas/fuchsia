#!/boot/bin/sh
#
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# This script runs all benchmarks for the Peridot layer.
#
# Usage: peridot_benchmarks.sh <output-dir>
# Example: peridot_benchmarks.sh /tmp

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
runbench_trace "${OUT_DIR}/ledger_get_same_page" /system/data/ledger/benchmark/get_same_page.tspec

# Exit with a code indicating whether any errors occurred.
runbench_exit

