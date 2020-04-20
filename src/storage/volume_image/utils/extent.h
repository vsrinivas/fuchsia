// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_UTILS_EXTENT_H_
#define SRC_STORAGE_VOLUME_IMAGE_UTILS_EXTENT_H_

#include <lib/fit/result.h>

#include <cstdint>
#include <tuple>

namespace storage::volume_image {

// An extent represents a collection of contiguous 'blocks' from a given offset in some device or
// block container. The size of the block is determined by the storage media.
class Extent {
 public:
  // The tail is the padding adding to fill the remainder of the last block, when converting between
  // extents of different block sizes.
  struct Tail {
    constexpr Tail() = default;
    constexpr Tail(uint64_t offset, uint64_t count) : offset(offset), count(count) {}

    // Returns true if the tail is empty.
    constexpr bool empty() const { return count == 0; }

    // Offset in bytes where the tail starts in the last block of the extent.
    uint64_t offset = 0;

    // Number of bytes in the tail.
    // This should be equal to the remainder of the block(block size -  offset).
    uint64_t count = 0;
  };

  constexpr Extent() = default;
  constexpr Extent(uint64_t offset, uint64_t count, uint64_t block_size)
      : offset_(offset), count_(count), block_size_(block_size) {}
  constexpr Extent(const Extent&) = default;
  constexpr Extent(Extent&&) = default;
  constexpr Extent& operator=(const Extent&) = default;
  constexpr Extent& operator=(Extent&&) = default;
  ~Extent() = default;

  // Returns a conversion of this extent to represent an extent in another storage medium at
  // |offset| with |block_size|.
  // |Tail| represents extra space added so the data in this extent is block aligned in the
  // converted extent.
  std::tuple<Extent, Tail> Convert(uint64_t offset, uint64_t block_size) const;

  // Returns the offset where this extent starts.
  constexpr uint64_t offset() const { return offset_; }

  // Returns the number of blocks contained in this extent.
  constexpr uint64_t count() const { return count_; }

  // Returns the block size by storage this extents represents.
  constexpr uint64_t block_size() const { return block_size_; }

  // Returns true if there are no blocks in this extent.
  constexpr bool empty() const { return begin() == end(); }

  // Returns the offset of the first block in the extent.
  constexpr uint64_t begin() const { return offset_; }

  // Returns non-inclusive offset past the last block.
  constexpr uint64_t end() const { return offset_ + count_; }

 private:
  // Offset in blocks where the extent starts.
  uint64_t offset_ = 0;

  // Number of blocks in this extent.
  uint64_t count_ = 0;

  // Block size in bytes used for this extent.
  uint64_t block_size_ = 0;
};

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_UTILS_EXTENT_H_
