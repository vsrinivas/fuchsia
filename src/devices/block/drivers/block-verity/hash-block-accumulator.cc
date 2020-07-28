// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hash-block-accumulator.h"

#include <cstring>

#include <digest/digest.h>

namespace block_verity {

HashBlockAccumulator::HashBlockAccumulator()
    : block{0}, block_bytes_filled(0), write_requested_(false) {}

void HashBlockAccumulator::Reset() {
  memset(block, 0, sizeof(block));
  block_bytes_filled = 0;
  write_requested_ = false;
}

bool HashBlockAccumulator::IsEmpty() const { return block_bytes_filled == 0; }

bool HashBlockAccumulator::IsFull() const { return block_bytes_filled >= kBlockSize; }

void HashBlockAccumulator::Feed(const uint8_t* buf, size_t count) {
  ZX_ASSERT(block_bytes_filled + count <= sizeof(block));
  memcpy(block + block_bytes_filled, buf, count);
  block_bytes_filled += count;
}

void HashBlockAccumulator::PadBlockWithZeroesToFill() {
  memset(block + block_bytes_filled, 0, kBlockSize - block_bytes_filled);
  block_bytes_filled = kBlockSize;
}

const uint8_t* HashBlockAccumulator::BlockData() const { return block; }

bool HashBlockAccumulator::HasWriteRequested() const { return write_requested_; }

void HashBlockAccumulator::MarkWriteRequested() { write_requested_ = true; }

}  // namespace block_verity
