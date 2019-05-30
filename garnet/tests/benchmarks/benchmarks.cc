// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This target runs all benchmarks for the Garnet layer.

#include "garnet/testing/benchmarking/benchmarking.h"
#include "src/lib/fxl/logging.h"

int main(int argc, const char** argv) {
  auto maybe_benchmarks_runner =
      benchmarking::BenchmarksRunner::Create(argc, argv);
  if (!maybe_benchmarks_runner) {
    exit(1);
  }

  auto& benchmarks_runner = *maybe_benchmarks_runner;

  std::string benchmarks_bot_name = benchmarks_runner.benchmarks_bot_name();

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
      // Passing this parameter reduces the test running time to something
      // reasonable -- 10 seconds for a transfer rate of 500 MiB/second.
      // (Otherwise biotime reads the whole device.)
      auto* kBytesToTransfer = "5G";

      std::string out_file = benchmarks_runner.MakeTempFile();
      benchmarks_runner.AddCustomBenchmark(
          "block_device_throughput",
          {"/boot/bin/biotime", "-total-bytes-to-transfer", kBytesToTransfer,
           "-output-file", out_file, block_device},
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
