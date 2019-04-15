// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This target runs all benchmarks for the Garnet layer.

#include "garnet/testing/benchmarking/benchmarking.h"
#include "garnet/tests/benchmarks/gfx_benchmarks.h"
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

  std::string benchmarks_bot_name = benchmarks_runner.benchmarks_bot_name();

  // TODO(PT-118): Input latency tests are only currently supported on NUC.
  if (benchmarks_bot_name == "garnet-x64-perf-dawson_canyon") {
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
  }

  AddGraphicsBenchmarks(&benchmarks_runner);

  // Test storage performance.
  if (benchmarks_bot_name == "garnet-x64-perf-dawson_canyon") {
    constexpr const char* block_device =
        "/dev/sys/pci/00:17.0/ahci/sata2/block";

    benchmarks_runner.AddTask([=]() {
      int status = benchmarking::Spawn({"/boot/bin/waitfor", "class=block",
                                        std::string("topo=") + block_device,
                                        "timeout=30000"});
      FXL_CHECK(status == 0);
    });

    // Test block device performance.
    {
      std::string out_file = benchmarks_runner.MakeTempFile();
      benchmarks_runner.AddCustomBenchmark(
          "block_device_throughput",
          {"/boot/bin/biotime", "-output-file", out_file, block_device},
          out_file);
    }

    // Test filesystem performance.
    benchmarks_runner.AddLibPerfTestBenchmark(
        "fs_bench", "/pkgfs/packages/garnet_benchmarks/0/test/fs/fs-bench-test",
        {"--fs", "minfs", "--block_device", block_device,
         "--print_statistics"});

    // Test BlobFs performance.
    benchmarks_runner.AddLibPerfTestBenchmark(
        "blobfs_bench",
        "/pkgfs/packages/garnet_benchmarks/0/test/sys/blobfs-bench-test",
        {"--fs", "blobfs", "--block_device", block_device,
         "--print_statistics"});
  } else if (benchmarks_bot_name == "garnet-arm64-perf-vim2") {
    // TODO(ZX-2466): Enable the storage perf tests on the VIM2 bots when we
    // figure out what partition or device we can use for testing.
    FXL_LOG(INFO) << "Storage perf tests skipped on bot '"
                  << benchmarks_bot_name << "'";
  } else {
    FXL_LOG(ERROR)
        << "Bot '" << benchmarks_bot_name
        << "' not recognized: please update benchmarks.cc in garnet.";
    exit(1);
  }

  // List block devices.  This is for debugging purposes and to help with
  // enabling the storage tests above on new devices.  We do this at the end
  // of this script because block devices aren't always immediately available
  // soon after boot, and because "waitfor" isn't applicable when we are
  // listing all devices.
  benchmarks_runner.AddTask([]() {
    FXL_LOG(INFO) << "-- block devices list (lsblk): start";
    int status = benchmarking::Spawn({"/boot/bin/lsblk"});
    FXL_CHECK(status == 0);
    FXL_LOG(INFO) << "-- block devices list (lsblk): end";
  });

  benchmarks_runner.Finish();
}
