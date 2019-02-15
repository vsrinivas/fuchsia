#!/boot/bin/sh
#
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# This script runs all benchmarks for the Peridot layer.
#
# For usage, see runbench_read_arguments in runbenchmarks.sh.

# Import the runbenchmarks library.
. /pkgfs/packages/runbenchmarks/0/data/runbenchmarks.sh

runbench_read_arguments "$@"

# Run "local" Ledger benchmarks.  These don't need external services to function
# properly.

runbench_exec "${OUT_DIR}/ledger.add_new_page_precached.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/add_new_page_precached.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.add_new_page_precached.json"

runbench_exec "${OUT_DIR}/ledger.add_new_page.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/add_new_page.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.add_new_page.json"

runbench_exec "${OUT_DIR}/ledger.get_same_page.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/get_same_page.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.get_same_page.json"

runbench_exec "${OUT_DIR}/ledger.get_page_id.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/get_page_id.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.get_page_id.json"

runbench_exec "${OUT_DIR}/ledger.get_small_entry.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/get_small_entry.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.get_small_entry.json"

runbench_exec "${OUT_DIR}/ledger.get_small_entry_inline.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/get_small_entry_inline.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.get_small_entry_inline.json"

runbench_exec "${OUT_DIR}/ledger.get_big_entry.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/get_big_entry.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.get_big_entry.json"

runbench_exec "${OUT_DIR}/ledger.put.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/put.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.put.json"

runbench_exec "${OUT_DIR}/ledger.put_as_reference.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/put_as_reference.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.put_as_reference.json"

runbench_exec "${OUT_DIR}/ledger.put_big_entry.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/put_big_entry.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.put_big_entry.json"

runbench_exec "${OUT_DIR}/ledger.transaction.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/transaction.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.transaction.json"

runbench_exec "${OUT_DIR}/ledger.update_entry.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/update_entry.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.update_entry.json"

runbench_exec "${OUT_DIR}/ledger.update_big_entry.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/update_big_entry.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.update_big_entry.json"

runbench_exec "${OUT_DIR}/ledger.update_entry_transactions.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/update_entry_transactions.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.update_entry_transactions.json"

runbench_exec "${OUT_DIR}/ledger.delete_entry.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/delete_entry.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.delete_entry.json"

runbench_exec "${OUT_DIR}/ledger.delete_big_entry.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/delete_big_entry.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.delete_big_entry.json"

runbench_exec "${OUT_DIR}/ledger.delete_entry_transactions.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/delete_entry_transactions.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.delete_entry_transactions.json"

runbench_exec "${OUT_DIR}/ledger.disk_space_empty_ledger.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/disk_space_empty_ledger.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.disk_space_empty_ledger.json"

runbench_exec "${OUT_DIR}/ledger.disk_space_empty_pages.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/disk_space_empty_pages.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.disk_space_empty_pages.json"

runbench_exec "${OUT_DIR}/ledger.disk_space_entries.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/disk_space_entries.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.disk_space_entries.json"

runbench_exec "${OUT_DIR}/ledger.disk_space_small_keys.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/disk_space_small_keys.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.disk_space_small_keys.json"

runbench_exec "${OUT_DIR}/ledger.disk_space_updates.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/disk_space_updates.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.disk_space_updates.json"

runbench_exec "${OUT_DIR}/ledger.disk_space_one_commit_per_entry.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/disk_space_one_commit_per_entry.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.disk_space_one_commit_per_entry.json"

runbench_exec "${OUT_DIR}/ledger.disk_space_cleared_page.json" \
    trace record \
    --spec-file=/pkgfs/packages/ledger_benchmarks/0/data/disk_space_cleared_page.tspec \
    --benchmark-results-file="${OUT_DIR}/ledger.disk_space_cleared_page.json"

runbench_exec "${OUT_DIR}/modular.story_runner.json" \
  trace record \
   --spec-file=/pkgfs/packages/modular_benchmarks/0/data/modular_benchmark_story.tspec \
   --test-suite=fuchsia.modular \
   --benchmark-results-file="${OUT_DIR}/modular.story_runner.json"

# Exit with a code indicating whether any errors occurred.
runbench_finish "${OUT_DIR}"
