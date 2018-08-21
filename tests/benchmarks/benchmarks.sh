#!/boot/bin/sh
#
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# This script runs all benchmarks for the Garnet layer.
#
# For usage, see runbench_read_arguments in runbenchmarks.sh.

# Import the runbenchmarks library.
. /pkgfs/packages/runbenchmarks/0/data/runbenchmarks.sh

runbench_read_arguments "$@"

# Benchmark example, here for demonstration.
runbench_exec "${OUT_DIR}/benchmark_example.json" \
    trace record \
    --spec-file=/system/data/benchmark_example/benchmark_example.tspec \
    --test-suite=fuchsia.benchmark_example \
    --benchmark-results-file="${OUT_DIR}/benchmark_example.json"

# Performance tests implemented in the Zircon repo.
runbench_exec "${OUT_DIR}/zircon.perf_test.json" \
    /system/test/sys/perf-test -p --out="${OUT_DIR}/zircon.perf_test.json"

# Performance tests implemented in the Garnet repo (the name
# "zircon_benchmarks" is now misleading).
runbench_exec "${OUT_DIR}/zircon_benchmarks.json" \
    /pkgfs/packages/zircon_benchmarks/0/test/zircon_benchmarks \
    -p --out="${OUT_DIR}/zircon_benchmarks.json"

if `run vulkan_is_supported`; then
  # Scenic performance tests.
  SCENIC_BENCHMARK="/pkgfs/packages/scenic_benchmarks/0/data/scenic_benchmark.sh"

  # hello_scenic
  runbench_exec "${OUT_DIR}/hello_scenic_benchmark.json" \
      "${SCENIC_BENCHMARK}" "${OUT_DIR}" \
      "${OUT_DIR}/hello_scenic_benchmark.json" \
      fuchsia.scenic.hello_scenic \
      "run hello_scenic"
else
  echo "Vulkan not supported; Scenic tests skipped."
fi

# Test storage performance.
# TODO(ZX-2466): Enable these tests for ARM64 hardware bots once they exist
# and have storage devices attached.
if [ "${benchmarks_bot_name}" = garnet-x64-perf-swift_canyon ]; then
  block_device=/dev/sys/pci/00:17.0/ahci/sata2/block
  waitfor class=block topo=${block_device} timeout=30000

  # Test block device performance.
  runbench_exec "${OUT_DIR}/block_device_throughput.json" \
      biotime -output-file "${OUT_DIR}/block_device_throughput.json" \
      ${block_device}

  # Test filesystem performance.
  runbench_exec "${OUT_DIR}/fs_bench.json" \
      /system/test/fs/fs-bench-test -p --fs minfs \
      --block_device ${block_device} \
      --print_statistics --out "${OUT_DIR}/fs_bench.json"
fi

# Exit with a code indicating whether any errors occurred.
runbench_finish "${OUT_DIR}"
