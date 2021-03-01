// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_TEST_MICRO_BENCHMARK_BLOCK_DEVICE_UTILS_H_
#define SRC_STORAGE_MINFS_TEST_MICRO_BENCHMARK_BLOCK_DEVICE_UTILS_H_

#include <fuchsia/hardware/block/llcpp/fidl.h>
#include <stdint.h>

namespace minfs_micro_benchmark {

using BlockFidlMetrics = ::llcpp::fuchsia::hardware::block::wire::BlockStats;

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

}  // namespace minfs_micro_benchmark

#endif  // SRC_STORAGE_MINFS_TEST_MICRO_BENCHMARK_BLOCK_DEVICE_UTILS_H_
