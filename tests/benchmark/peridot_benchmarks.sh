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

# Run "local" Ledger benchmarks.  These don't need external services to function
# properly.

runbench_exec "${OUT_DIR}/ledger.add_new_page.json" \
    trace record \
    --spec-file=/system/data/ledger/benchmark/add_new_page.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.add_new_page.json"

runbench_exec "${OUT_DIR}/ledger.get_same_page.json" \
    trace record \
    --spec-file=/system/data/ledger/benchmark/get_same_page.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.get_same_page.json"

runbench_exec "${OUT_DIR}/ledger.put.json" \
    trace record \
    --spec-file=/system/data/ledger/benchmark/put.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.put.json"

runbench_exec "${OUT_DIR}/ledger.put_as_reference.json" \
    trace record \
    --spec-file=/system/data/ledger/benchmark/put_as_reference.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.put_as_reference.json"

runbench_exec "${OUT_DIR}/ledger.transaction.json" \
    trace record \
    --spec-file=/system/data/ledger/benchmark/transaction.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.transaction.json"

# Exit with a code indicating whether any errors occurred.
runbench_finish "${OUT_DIR}"
