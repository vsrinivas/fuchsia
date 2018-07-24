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

# TODO(LE-425): Fix & re-enable this test.
# runbench_exec "${OUT_DIR}/ledger.add_new_page.json" \
#    trace record \
#    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/add_new_page.tspec \
#    --test-suite=fuchsia.ledger.add_new_page \
#    --benchmark-results-file="${OUT_DIR}/ledger.add_new_page.json"

runbench_exec "${OUT_DIR}/ledger.get_same_page.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/get_same_page.tspec \
    --test-suite=fuchsia.ledger.get_same_page \
    --benchmark-results-file="${OUT_DIR}/ledger.get_same_page.json"

runbench_exec "${OUT_DIR}/ledger.put.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/put.tspec \
    --test-suite=fuchsia.ledger.put \
    --benchmark-results-file="${OUT_DIR}/ledger.put.json"

runbench_exec "${OUT_DIR}/ledger.put_as_reference.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/put_as_reference.tspec \
    --test-suite=fuchsia.ledger.put_as_reference \
    --benchmark-results-file="${OUT_DIR}/ledger.put_as_reference.json"

runbench_exec "${OUT_DIR}/ledger.put_big_entry.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/put_big_entry.tspec \
    --test-suite=fuchsia.ledger.put_big_entry \
    --benchmark-results-file="${OUT_DIR}/ledger.put_big_entry.json"

runbench_exec "${OUT_DIR}/ledger.transaction.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/transaction.tspec \
    --test-suite=fuchsia.ledger.transaction \
    --benchmark-results-file="${OUT_DIR}/ledger.transaction.json"

runbench_exec "${OUT_DIR}/ledger.update_entry.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/update_entry.tspec \
    --test-suite=fuchsia.ledger.update_entry \
    --benchmark-results-file="${OUT_DIR}/ledger.update_entry.json"

runbench_exec "${OUT_DIR}/ledger.update_big_entry.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/update_big_entry.tspec \
    --test-suite=fuchsia.ledger.update_big_entry \
    --benchmark-results-file="${OUT_DIR}/ledger.update_big_entry.json"

runbench_exec "${OUT_DIR}/ledger.update_entry_transactions.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/update_entry_transactions.tspec \
    --test-suite=fuchsia.ledger.update_entry_transactions \
    --benchmark-results-file="${OUT_DIR}/ledger.update_entry_transactions.json"

runbench_exec "${OUT_DIR}/ledger.delete_entry.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/delete_entry.tspec \
    --test-suite=fuchsia.ledger.delete_entry \
    --benchmark-results-file="${OUT_DIR}/ledger.delete_entry.json"

runbench_exec "${OUT_DIR}/ledger.delete_big_entry.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/delete_big_entry.tspec \
    --test-suite=fuchsia.ledger.delete_big_entry \
    --benchmark-results-file="${OUT_DIR}/ledger.delete_big_entry.json"

runbench_exec "${OUT_DIR}/ledger.delete_entry_transactions.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/delete_entry_transactions.tspec \
    --test-suite=fuchsia.ledger.delete_entry_transactions \
    --benchmark-results-file="${OUT_DIR}/ledger.delete_entry_transactions.json"

# Exit with a code indicating whether any errors occurred.
runbench_finish "${OUT_DIR}"
