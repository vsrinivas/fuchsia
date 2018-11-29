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
    --spec-file=/pkgfs/packages/benchmark/0/data/benchmark_example.tspec \
    --benchmark-results-file="${OUT_DIR}/benchmark_example.json"

# Performance tests implemented in the Zircon repo.
runbench_exec "${OUT_DIR}/zircon.perf_test.json" \
    /system/test/sys/perf-test -p --out="${OUT_DIR}/zircon.perf_test.json"

# Performance tests implemented in the Garnet repo (the name
# "zircon_benchmarks" is now misleading).
runbench_exec "${OUT_DIR}/zircon_benchmarks.json" \
    /pkgfs/packages/zircon_benchmarks/0/test/zircon_benchmarks \
    -p --out="${OUT_DIR}/zircon_benchmarks.json"

vulkan_is_supported_result="$(/pkgfs/packages/run/0/bin/run vulkan_is_supported || echo '')"
if [ "${vulkan_is_supported_result}" = '1' ]; then
  # Run the gfx benchmarks in the current shell environment, because they write
  # to (hidden) global state used by runbench_finish.
  . /pkgfs/packages/garnet_benchmarks/0/bin/gfx_benchmarks.sh "$@"
elif [ "${vulkan_is_supported_result}" = '0' ]; then
  echo 'Vulkan not supported; graphics tests skipped.'
else
  echo 'Error: Failed to run vulkan_is_supported'
  exit 1
fi

# Test storage performance.
# TODO(ZX-2466): Enable these tests for ARM64 hardware bots once they exist
# and have storage devices attached.
if [ "${benchmarks_bot_name}" = garnet-x64-perf-dawson_canyon ]; then
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

  # Test BlobFs performance.
  runbench_exec "${OUT_DIR}/blobfs_bench.json" \
      /system/test/sys/blobfs-bench-test -p --fs blobfs \
      --block_device ${block_device} \
      --print_statistics --out "${OUT_DIR}/blobfs_bench.json"
fi

# List block devices.  This is for debugging purposes and to help with
# enabling the storage tests above on new devices.  We do this at the end
# of this script because block devices aren't always immediately available
# soon after boot, and because "waitfor" isn't applicable when we are
# listing all devices.
echo "-- block devices list (lsblk): start"
lsblk
echo "-- block devices list (lsblk): end"

# Exit with a code indicating whether any errors occurred.
runbench_finish "${OUT_DIR}"
