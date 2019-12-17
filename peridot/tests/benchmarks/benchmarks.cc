// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This target runs all benchmarks for the Peridot layer.

#include "garnet/testing/benchmarking/benchmarking.h"
#include "peridot/tests/benchmarks/gfx_benchmarks.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace {

void AddPerfTests(benchmarking::BenchmarksRunner* benchmarks_runner, bool perfcompare_mode) {
  FXL_DCHECK(benchmarks_runner);

  // Benchmark example, here for demonstration.
  benchmarks_runner->AddTspecBenchmark("benchmark_example",
                                       "/pkgfs/packages/benchmark/0/data/benchmark_example.tspec");

  // For the perfcompare CQ trybot, we run the libperftest-based processes
  // multiple times.  That is useful for tests that exhibit between-process
  // variation in results (e.g. due to memory layout chosen when a process
  // starts) -- it reduces the variation in the average that we report.
  //
  // Ideally we would do the same for non-perfcompare mode, i.e. for the
  // results that get uploaded to the Catapult dashboard by the perf bots
  // on CI.  However, catapult_converter does not yet support merging
  // results from multiple process runs.  (That is partly because
  // catapult_converter is run separately on the results from each process
  // run.)
  if (perfcompare_mode) {
    // Reduce the number of iterations of each perf test within each
    // process given that we are launching each process multiple times.
    std::vector<std::string> extra_args = {"--quiet", "--runs", "100"};

    for (int process = 0; process < 6; ++process) {
      // Performance tests implemented in the Zircon repo.
      benchmarks_runner->AddLibPerfTestBenchmark(
          fxl::StringPrintf("zircon.perf_test_process%06d", process),
          "/pkgfs/packages/fuchsia_benchmarks/0/test/sys/perf-test", extra_args);

      // Performance tests implemented in the Garnet repo (the name
      // "zircon_benchmarks" is now misleading).
      benchmarks_runner->AddLibPerfTestBenchmark(
          fxl::StringPrintf("zircon_benchmarks_process%06d", process),
          "/pkgfs/packages/zircon_benchmarks/0/test/zircon_benchmarks", extra_args);
    }
  } else {
    std::vector<std::string> extra_args = {"--quiet"};

    // Performance tests implemented in the Zircon repo.
    benchmarks_runner->AddLibPerfTestBenchmark(
        "zircon.perf_test", "/pkgfs/packages/fuchsia_benchmarks/0/test/sys/perf-test", extra_args);

    // Performance tests implemented in the Garnet repo (the name
    // "zircon_benchmarks" is now misleading).
    benchmarks_runner->AddLibPerfTestBenchmark(
        "zircon_benchmarks", "/pkgfs/packages/zircon_benchmarks/0/test/zircon_benchmarks",
        extra_args);
  }

  // Fuchsia inspect Rust benchmarks.
  benchmarks_runner->AddTspecBenchmark(
      "rust_inspect_bench", "/pkgfs/packages/rust_inspect_benchmarks/0/data/benchmarks.tspec");

  // Run "local" Ledger benchmarks.  These don't need external services to
  // function properly.
  //
  // This list should be kept in sync with the list in
  // src/tests/end_to_end/perf/test/ledger_perf_test.dart until this list
  // is removed (TODO(fxb/23091)).

  // clang-format off

  // TODO(fxb/42576): Disabled test due to flakiness.
  // benchmarks_runner->AddTspecBenchmark("ledger.add_new_page_after_clear", "/pkgfs/packages/ledger_benchmarks/0/data/add_new_page_after_clear.tspec");

  benchmarks_runner->AddTspecBenchmark("ledger.add_new_page_precached", "/pkgfs/packages/ledger_benchmarks/0/data/add_new_page_precached.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.add_new_page", "/pkgfs/packages/ledger_benchmarks/0/data/add_new_page.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.get_same_page", "/pkgfs/packages/ledger_benchmarks/0/data/get_same_page.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.get_page_id", "/pkgfs/packages/ledger_benchmarks/0/data/get_page_id.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.get_small_entry", "/pkgfs/packages/ledger_benchmarks/0/data/get_small_entry.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.get_small_entry_inline", "/pkgfs/packages/ledger_benchmarks/0/data/get_small_entry_inline.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.get_big_entry", "/pkgfs/packages/ledger_benchmarks/0/data/get_big_entry.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.put", "/pkgfs/packages/ledger_benchmarks/0/data/put.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.put_as_reference", "/pkgfs/packages/ledger_benchmarks/0/data/put_as_reference.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.put_big_entry", "/pkgfs/packages/ledger_benchmarks/0/data/put_big_entry.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.transaction", "/pkgfs/packages/ledger_benchmarks/0/data/transaction.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.update_entry", "/pkgfs/packages/ledger_benchmarks/0/data/update_entry.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.update_big_entry", "/pkgfs/packages/ledger_benchmarks/0/data/update_big_entry.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.update_entry_transactions", "/pkgfs/packages/ledger_benchmarks/0/data/update_entry_transactions.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.delete_entry", "/pkgfs/packages/ledger_benchmarks/0/data/delete_entry.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.delete_big_entry", "/pkgfs/packages/ledger_benchmarks/0/data/delete_big_entry.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.delete_entry_transactions", "/pkgfs/packages/ledger_benchmarks/0/data/delete_entry_transactions.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.disk_space_empty_ledger", "/pkgfs/packages/ledger_benchmarks/0/data/disk_space_empty_ledger.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.disk_space_empty_pages", "/pkgfs/packages/ledger_benchmarks/0/data/disk_space_empty_pages.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.disk_space_entries", "/pkgfs/packages/ledger_benchmarks/0/data/disk_space_entries.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.disk_space_small_keys", "/pkgfs/packages/ledger_benchmarks/0/data/disk_space_small_keys.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.disk_space_updates", "/pkgfs/packages/ledger_benchmarks/0/data/disk_space_updates.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.disk_space_one_commit_per_entry", "/pkgfs/packages/ledger_benchmarks/0/data/disk_space_one_commit_per_entry.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.disk_space_cleared_page", "/pkgfs/packages/ledger_benchmarks/0/data/disk_space_cleared_page.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.put_memory", "/pkgfs/packages/ledger_benchmarks/0/data/put_memory.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.stories_single_active", "/pkgfs/packages/ledger_benchmarks/0/data/stories_single_active.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.stories_many_active", "/pkgfs/packages/ledger_benchmarks/0/data/stories_many_active.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.stories_wait_cached", "/pkgfs/packages/ledger_benchmarks/0/data/stories_wait_cached.tspec");
  benchmarks_runner->AddTspecBenchmark("ledger.stories_memory", "/pkgfs/packages/ledger_benchmarks/0/data/stories_memory.tspec");
  // clang-format on

  // TODO(PT-181, PT-182): The following input latency and graphics benchmarks
  // do not make an effort to close the graphics application being benchmarked
  // at exit (the app will continue to run even after the benchmark driver
  // process has exited).  Because of this, it is important that they run at the
  // end, so that the residual graphics application is not running during other
  // benchmarks. The long term plan is to migrate them away from here and into
  // the e2e testing framework, which is tracked in the TODO bugs.

  // TODO(PT-118): Input latency tests are only currently supported on NUC.
#if !defined(__aarch64__)
  // simplest_app
  {
    constexpr const char* kLabel = "fuchsia.input_latency.simplest_app";
    std::string out_file = benchmarks_runner->MakePerfResultsOutputFilename(kLabel);
    benchmarks_runner->AddCustomBenchmark(
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
    std::string out_file = benchmarks_runner->MakePerfResultsOutputFilename(kLabel);
    benchmarks_runner->AddCustomBenchmark(
        kLabel,
        {"/bin/run",
         "fuchsia-pkg://fuchsia.com/garnet_input_latency_benchmarks#meta/"
         "run_yuv_to_image_pipe_benchmark.cmx",
         "--out_file", out_file, "--benchmark_label", kLabel},
        out_file);
  }
#endif

  AddGraphicsBenchmarks(benchmarks_runner);
}

}  // namespace

int main(int argc, const char** argv) {
  bool perfcompare_mode = false;
  if (argc >= 2 && strcmp(argv[1], "--perfcompare_mode") == 0) {
    perfcompare_mode = true;
    // Remove argv[1] from the argument list.
    for (int i = 2; i < argc; ++i)
      argv[i - 1] = argv[i];
    --argc;
  }

  auto maybe_benchmarks_runner = benchmarking::BenchmarksRunner::Create(argc, argv);
  if (!maybe_benchmarks_runner) {
    exit(1);
  }

  auto& benchmarks_runner = *maybe_benchmarks_runner;
  AddPerfTests(&benchmarks_runner, perfcompare_mode);

  benchmarks_runner.Finish();
}
