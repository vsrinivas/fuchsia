// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_HASH_BLOCK_ACCUMULATOR_H_
#define SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_HASH_BLOCK_ACCUMULATOR_H_

#include "src/devices/block/drivers/block-verity/constants.h"

namespace block_verity {

// A class that contains a block-sized buffer, a write pointer, and a boolean
// for tracking whether we've issued a write for the contents yet or not.  We
// use this class to accumulate the hashes of several blocks we read before
// writing back a completed integrity block.
//
// Future work could genericize this over block size and hash algorithm,
// but for now it's expedient to assume 4k and SHA256 from constants.h.
class HashBlockAccumulator {
 public:
  HashBlockAccumulator();
  ~HashBlockAccumulator() = default;

  // Zero the block buffer, reset the write offset `block_bytes_filled` to zero,
  // and set `write_requested_` to false.
  void Reset();

  // True if block_bytes_filled is zero -- no bytes have been fed since `Reset`
  // was last called or construction.
  bool IsEmpty() const;

  // True if `block_bytes_filled` is `kBlockSize`.  Semantically, this block is
  // full and ready to be written out to backing storage.
  bool IsFull() const;

  // Copy `count` bytes from `buf` to the next unset `block`, and increment
  // `block_bytes_filled` by `count`.  This is called with the hash of some
  // lower block in the hash tree - either a leaf data block, or a lower-level
  // integrity block.
  void Feed(const uint8_t* buf, size_t count);

  // Feeds zeroes into the buffer until the block is full.
  void PadBlockWithZeroesToFill();

  // Retrievs the block buffer for writeback purposes.
  const uint8_t* BlockData() const;

  // Accessor/mutator for bookkeeping a bit tracking whether we've attempted to
  // write this block (once filled) back to underlying storage yet.
  bool HasWriteRequested() const;
  void MarkWriteRequested();

 private:
  uint8_t block[kBlockSize];
  size_t block_bytes_filled;
  bool write_requested_;
};

}  // namespace block_verity

#endif  // SRC_DEVICES_BLOCK_DRIVERS_BLOCK_VERITY_HASH_BLOCK_ACCUMULATOR_H_
