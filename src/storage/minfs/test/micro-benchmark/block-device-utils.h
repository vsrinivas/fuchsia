// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_TEST_MICRO_BENCHMARK_BLOCK_DEVICE_UTILS_H_
#define SRC_STORAGE_MINFS_TEST_MICRO_BENCHMARK_BLOCK_DEVICE_UTILS_H_

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/storage/metrics/c/fidl.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fit/result.h>
#include <limits.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <optional>

#include <ramdevice-client/ramdisk.h>
#include <storage-metrics/block-metrics.h>
#include <zxtest/zxtest.h>

namespace minfs_micro_benchmanrk {

using BlockFidlMetrics = fuchsia_hardware_block_BlockStats;

constexpr uint8_t BitsPerByte = 8;

struct BlockDeviceSizes {
  uint64_t block_size;
  uint64_t block_count;

  uint64_t BlockSize() const { return block_size; }

  uint64_t BitsPerBlock() const { return block_size * BitsPerByte; }

  uint64_t BitsToBlocks(uint64_t bits) const {
    return (bits + BitsPerBlock() - 1) / BitsPerBlock();
  }

  uint64_t BytesToBlocks(uint64_t bytes) const { return (bytes + BlockSize() - 1) / BlockSize(); }
};

class BlockDevice {
 public:
  BlockDevice() = delete;
  BlockDevice(const BlockDevice&) = delete;
  BlockDevice(BlockDevice&&) = delete;
  BlockDevice& operator=(const BlockDevice&) = delete;
  BlockDevice& operator=(BlockDevice&&) = delete;

  BlockDevice(const BlockDeviceSizes& sizes);

  const char* Path() const {
    if (ramdisk_ == nullptr) {
      return nullptr;
    }
    return path_;
  }

  int BlockFd() const { return ramdisk_ == nullptr ? -1 : ramdisk_get_block_fd(ramdisk_); }

  ~BlockDevice() { CleanUp(); }

 private:
  void CleanUp();
  devmgr_integration_test::IsolatedDevmgr isolated_devmgr_ = {};
  ramdisk_client_t* ramdisk_ = {};
  char path_[PATH_MAX] = {};
};

}  // namespace minfs_micro_benchmanrk

#endif  // SRC_STORAGE_MINFS_TEST_MICRO_BENCHMARK_BLOCK_DEVICE_UTILS_H_
