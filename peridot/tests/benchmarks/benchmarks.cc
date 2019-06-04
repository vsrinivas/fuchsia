// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This target runs all benchmarks for the Peridot layer.

#include "garnet/testing/benchmarking/benchmarking.h"
#include "peridot/tests/benchmarks/gfx_benchmarks.h"
#include "src/lib/fxl/logging.h"

int main(int argc, const char** argv) {
  auto maybe_benchmarks_runner =
      benchmarking::BenchmarksRunner::Create(argc, argv);
  if (!maybe_benchmarks_runner) {
    exit(1);
  }

  auto& benchmarks_runner = *maybe_benchmarks_runner;

  // Benchmark example, here for demonstration.
  benchmarks_runner.AddTspecBenchmark(
      "benchmark_example",
      "/pkgfs/packages/benchmark/0/data/benchmark_example.tspec");

  // Performance tests implemented in the Zircon repo.
  benchmarks_runner.AddLibPerfTestBenchmark(
      "zircon.perf_test",
      "/pkgfs/packages/garnet_benchmarks/0/test/sys/perf-test");

  // Performance tests implemented in the Garnet repo (the name
  // "zircon_benchmarks" is now misleading).
  benchmarks_runner.AddLibPerfTestBenchmark(
      "zircon_benchmarks",
      "/pkgfs/packages/zircon_benchmarks/0/test/zircon_benchmarks");

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

  // TODO(PT-181, PT-182): The following input latency and graphics benchmarks
  // do not make an effort to close the graphics application being benchmarked
  // at exit (the app will continue to run even after the benchmark driver
  // process has exited).  Because of this, it is important that they run at the
  // end, so that the residual graphics application is not running during other
  // benchmarks. The long term plan is to migrate them away from here and into
  // the e2e testing framework, which is tracked in the TODO bugs.

  std::string benchmarks_bot_name = benchmarks_runner.benchmarks_bot_name();

  // TODO(PT-118): Input latency tests are only currently supported on NUC.
  if (benchmarks_bot_name == "peridot-x64-perf-dawson_canyon") {
    // simplest_app
    {
      constexpr const char* kLabel = "fuchsia.input_latency.simplest_app";
      std::string out_file = benchmarks_runner.MakeTempFile();
      benchmarks_runner.AddCustomBenchmark(
          kLabel,
          {"/bin/run",
           "fuchsia-pkg://fuchsia.com/garnet_input_latency_benchmarks#meta/"
           "run_simplest_app_benchmark.cmx",
           "--out_file", out_file, "--benchmark_label", kLabel},
          out_file);
    }
    // yuv_to_image_pipe
    {
      constexpr const char* kLabel = "fuchsia.input_latency.yuv_to_image_pipe";
      std::string out_file = benchmarks_runner.MakeTempFile();
      benchmarks_runner.AddCustomBenchmark(
          kLabel,
          {"/bin/run",
           "fuchsia-pkg://fuchsia.com/garnet_input_latency_benchmarks#meta/"
           "run_yuv_to_image_pipe_benchmark.cmx",
           "--out_file", out_file, "--benchmark_label", kLabel},
          out_file);
    }
  } else if (benchmarks_bot_name == "peridot-arm64-perf-vim2") {
    FXL_LOG(INFO) << "Input latency tests skipped on bot '"
                  << benchmarks_bot_name << '\'';
  } else {
    FXL_LOG(ERROR)
        << "Bot '" << benchmarks_bot_name
        << "' not recognized: please update benchmarks.cc in peridot.";
    exit(1);
  }

  AddGraphicsBenchmarks(&benchmarks_runner);

  benchmarks_runner.Finish();
}
