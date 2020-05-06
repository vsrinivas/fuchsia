// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/testing/benchmarking/benchmarking.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace {

void AddPerfTests(benchmarking::BenchmarksRunner* benchmarks_runner, bool perfcompare_mode) {
  FX_DCHECK(benchmarks_runner);

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
      benchmarks_runner->AddLibPerfTestBenchmark(
          fxl::StringPrintf("fuchsia_microbenchmarks_process%06d", process),
          "/bin/fuchsia_microbenchmarks", extra_args);
    }
  } else {
    std::vector<std::string> extra_args = {"--quiet"};

    benchmarks_runner->AddLibPerfTestBenchmark("fuchsia_microbenchmarks",
                                               "/bin/fuchsia_microbenchmarks", extra_args);
  }

  // Fuchsia inspect Rust benchmarks.
  benchmarks_runner->AddTspecBenchmark(
      "rust_inspect_bench", "/pkgfs/packages/rust_inspect_benchmarks/0/data/benchmarks.tspec");

  // Run netstack benchmarks.
  benchmarks_runner->AddTspecBenchmark("netstack.udp_micro_benchmarks", "/pkgfs/packages/netstack_benchmarks/0/data/udp_benchmark.tspec");

  // clang-format on

  // Kernel boot timeline.
  {
    constexpr const char* kLabel = "fuchsia.kernel.boot";
    std::string out_file = benchmarks_runner->MakePerfResultsOutputFilename(kLabel);
    benchmarks_runner->AddCustomBenchmark(kLabel, {"/bin/kernel-boot-timeline", out_file},
                                          out_file);
  }

  // FIDL benchmarks.
  {
    benchmarks_runner->AddLibPerfTestBenchmark("fidl_microbenchmarks.lib_fidl",
                                               "/bin/lib_fidl_microbenchmarks",
                                               std::vector<std::string>());
  }
  {
    constexpr const char* kLabel = "fidl_microbenchmarks.go";
    std::string out_file = benchmarks_runner->MakePerfResultsOutputFilename(kLabel);
    benchmarks_runner->AddCustomBenchmark(
        kLabel, {"/bin/go_fidl_microbenchmarks", "--encode_counts", "--out_file", out_file},
        out_file);
  }
  {
    benchmarks_runner->AddLibPerfTestBenchmark("fidl_microbenchmarks.hlcpp",
                                               "/bin/hlcpp_fidl_microbenchmarks",
                                               std::vector<std::string>());
  }
  {
    constexpr const char* kLabel = "fidl_microbenchmarks.rust";
    std::string out_file = benchmarks_runner->MakePerfResultsOutputFilename(kLabel);
    benchmarks_runner->AddCustomBenchmark(kLabel, {"/bin/rust_fidl_microbenchmarks", out_file},
                                          out_file);
  }
  {
    benchmarks_runner->AddLibPerfTestBenchmark("fidl_microbenchmarks.llcpp",
                                               "/bin/llcpp_fidl_microbenchmarks",
                                               std::vector<std::string>());
  }
  {
    constexpr const char* kLabel = "fidl_microbenchmarks.roundtrip";
    std::string out_file = benchmarks_runner->MakePerfResultsOutputFilename(kLabel);
    benchmarks_runner->AddCustomBenchmark("fidl_roundtrip",
                                          {"/bin/roundtrip_fidl_benchmarks", out_file}, out_file);
  }
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
