// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_BLOCK_UTILS_H_
#define SRC_STORAGE_MINFS_BLOCK_UTILS_H_

#include <lib/zx/status.h>

#include <range/range.h>

namespace minfs {

using ByteRange = range::Range<uint64_t>;
using BlockRange = range::Range<uint64_t>;

// Represents a block on a device. The block should be relative to the start of the device, and the
// block size is that used by the file system. The block can also be unmapped a.k.a. sparse. Files
// that are unmapped blocks are zeroed; they occupy no space on the disk, but the user sees zeroed
// data.
class DeviceBlock {
 public:
  static DeviceBlock Unmapped() { return {}; }

  DeviceBlock() = default;
  DeviceBlock(uint64_t block) : block_(block) { ZX_ASSERT(block != kUnmapped); }

  DeviceBlock(const DeviceBlock& other) = default;
  DeviceBlock& operator=(const DeviceBlock& other) = default;

  bool IsMapped() const { return block_ != kUnmapped; }
  uint64_t block() const {
    ZX_ASSERT(block_ != kUnmapped);
    return block_;
  }

  bool operator==(const DeviceBlock& other) const { return block_ == other.block_; }
  bool operator!=(const DeviceBlock& other) const { return block_ != other.block_; }

 private:
  static constexpr uint64_t kUnmapped = std::numeric_limits<uint64_t>::max();

  uint64_t block_ = kUnmapped;
};

class DeviceBlockRange {
 public:
  DeviceBlockRange() = default;
  DeviceBlockRange(DeviceBlock device_block, uint64_t count)
      : device_block_(device_block), count_(count) {}

  DeviceBlockRange(const DeviceBlockRange& other) = default;
  DeviceBlockRange& operator=(const DeviceBlockRange& other) = default;

  DeviceBlock device_block() const { return device_block_; }
  bool IsMapped() const { return device_block_.IsMapped(); }
  uint64_t block() const { return device_block_.block(); }
  uint64_t count() const { return count_; }

 private:
  DeviceBlock device_block_;
  uint64_t count_ = 0;
};

// Given a byte range, returns a block range that covers the byte range.
static inline BlockRange BytesToBlocks(ByteRange range, unsigned block_size) {
  return BlockRange(range.Start() / block_size, (range.End() + block_size - 1) / block_size);
}

// Helpers that will call |callback| for all the blocks that encompass the range, which
// is in blocks. |callback| is of the form:
//
//   zx::status<uint64_t> callback(range::Range<BlockType> range);
//
// |callback| can modify |block_count| to indicate how many blocks it processed if less than all of
// the blocks, or leave it unchanged if all blocks are processed.
template <typename BlockType, typename F>
[[nodiscard]] zx::status<> EnumerateBlocks(range::Range<BlockType> range, F callback) {
  uint64_t len;
  BlockType block = range.Start();
  for (; block < range.End(); block += len) {
    zx::status<uint64_t> status = callback(range::Range(block, range.End()));
    if (status.is_error())
      return status.take_error();
    len = status.value();
    ZX_DEBUG_ASSERT(len > 0);
  }
  return zx::ok();
}

// Same, but for bytes rather than blocks. It will enumerate all blocks touched by the range.
template <typename F>
[[nodiscard]] zx::status<> EnumerateBlocks(ByteRange range, unsigned block_size, F callback) {
  if (range.Length() == 0)
    return zx::ok();
  return EnumerateBlocks(BytesToBlocks(range, block_size), callback);
}

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_BLOCK_UTILS_H_
