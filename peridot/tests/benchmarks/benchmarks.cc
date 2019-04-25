// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This target runs all benchmarks for the Peridot layer.

#include "garnet/testing/benchmarking/benchmarking.h"
#include "src/lib/fxl/logging.h"

int main(int argc, const char** argv) {
  auto maybe_benchmarks_runner =
      benchmarking::BenchmarksRunner::Create(argc, argv);
  if (!maybe_benchmarks_runner) {
    exit(1);
  }

  auto& benchmarks_runner = *maybe_benchmarks_runner;

  // Run "local" Ledger benchmarks.  These don't need external services to
  // function properly.

  // clang-format off
  benchmarks_runner.AddTspecBenchmark("ledger.add_new_page_after_clear", "/pkgfs/packages/ledger_benchmarks/0/data/add_new_page_after_clear.tspec");
  benchmarks_runner.AddTspecBenchmark("ledger.add_new_page_precached", "/pkgfs/packages/ledger_benchmarks/0/data/add_new_page_precached.tspec");
  benchmarks_runner.AddTspecBenchmark("ledger.add_new_page", "/pkgfs/packages/ledger_benchmarks/0/data/add_new_page.tspec");
  benchmarks_runner.AddTspecBenchmark("ledger.get_same_page", "/pkgfs/packages/ledger_benchmarks/0/data/get_same_page.tspec");
  benchmarks_runner.AddTspecBenchmark("ledger.get_page_id", "/pkgfs/packages/ledger_benchmarks/0/data/get_page_id.tspec");
  benchmarks_runner.AddTspecBenchmark("ledger.get_small_entry", "/pkgfs/packages/ledger_benchmarks/0/data/get_small_entry.tspec");
  benchmarks_runner.AddTspecBenchmark("ledger.get_small_entry_inline", "/pkgfs/packages/ledger_benchmarks/0/data/get_small_entry_inline.tspec");
  benchmarks_runner.AddTspecBenchmark("ledger.get_big_entry", "/pkgfs/packages/ledger_benchmarks/0/data/get_big_entry.tspec");
  benchmarks_runner.AddTspecBenchmark("ledger.put", "/pkgfs/packages/ledger_benchmarks/0/data/put.tspec");
  benchmarks_runner.AddTspecBenchmark("ledger.put_as_reference", "/pkgfs/packages/ledger_benchmarks/0/data/put_as_reference.tspec");
  benchmarks_runner.AddTspecBenchmark("ledger.put_big_entry", "/pkgfs/packages/ledger_benchmarks/0/data/put_big_entry.tspec");
  benchmarks_runner.AddTspecBenchmark("ledger.transaction", "/pkgfs/packages/ledger_benchmarks/0/data/transaction.tspec");
  benchmarks_runner.AddTspecBenchmark("ledger.update_entry", "/pkgfs/packages/ledger_benchmarks/0/data/update_entry.tspec");
  benchmarks_runner.AddTspecBenchmark("ledger.update_big_entry", "/pkgfs/packages/ledger_benchmarks/0/data/update_big_entry.tspec");
  benchmarks_runner.AddTspecBenchmark("ledger.update_entry_transactions", "/pkgfs/packages/ledger_benchmarks/0/data/update_entry_transactions.tspec");
  benchmarks_runner.AddTspecBenchmark("ledger.delete_entry", "/pkgfs/packages/ledger_benchmarks/0/data/delete_entry.tspec");
  benchmarks_runner.AddTspecBenchmark("ledger.delete_big_entry", "/pkgfs/packages/ledger_benchmarks/0/data/delete_big_entry.tspec");
  benchmarks_runner.AddTspecBenchmark("ledger.delete_entry_transactions", "/pkgfs/packages/ledger_benchmarks/0/data/delete_entry_transactions.tspec");
  benchmarks_runner.AddTspecBenchmark("ledger.disk_space_empty_ledger", "/pkgfs/packages/ledger_benchmarks/0/data/disk_space_empty_ledger.tspec");
  benchmarks_runner.AddTspecBenchmark("ledger.disk_space_empty_pages", "/pkgfs/packages/ledger_benchmarks/0/data/disk_space_empty_pages.tspec");
  benchmarks_runner.AddTspecBenchmark("ledger.disk_space_entries", "/pkgfs/packages/ledger_benchmarks/0/data/disk_space_entries.tspec");
  benchmarks_runner.AddTspecBenchmark("ledger.disk_space_small_keys", "/pkgfs/packages/ledger_benchmarks/0/data/disk_space_small_keys.tspec");
  benchmarks_runner.AddTspecBenchmark("ledger.disk_space_updates", "/pkgfs/packages/ledger_benchmarks/0/data/disk_space_updates.tspec");
  benchmarks_runner.AddTspecBenchmark("ledger.disk_space_one_commit_per_entry", "/pkgfs/packages/ledger_benchmarks/0/data/disk_space_one_commit_per_entry.tspec");
  benchmarks_runner.AddTspecBenchmark("ledger.disk_space_cleared_page", "/pkgfs/packages/ledger_benchmarks/0/data/disk_space_cleared_page.tspec");
  benchmarks_runner.AddTspecBenchmark("ledger.put_memory", "/pkgfs/packages/ledger_benchmarks/0/data/put_memory.tspec");
  benchmarks_runner.AddTspecBenchmark("modular.story_runner.json", "/pkgfs/packages/modular_benchmarks/0/data/modular_benchmark_story.tspec", "fuchsia.modular");
  // clang-format on

  benchmarks_runner.Finish();
}
