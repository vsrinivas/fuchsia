// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <blobfs/format.h>
#include <blobfs/iterator/extent-iterator.h>
#include <fbl/function.h>
#include <fs/trace.h>

#include <zircon/types.h>

namespace blobfs {

// Wraps an ExtentIterator to allow traversal of a node in block-order rather
// than extent-order.
class BlockIterator {
 public:
  BlockIterator(ExtentIterator* iterator);
  DISALLOW_COPY_ASSIGN_AND_MOVE(BlockIterator);

  // Returns true if there are no more blocks to be consumed.
  bool Done() const;

  // Returns the number of blocks we've iterated past in total.
  uint64_t BlockIndex() const;

  // Acquires up to |length| additional blocks.
  // Postcondition: |out_length| <= |length|.
  //
  // Returns the actual number of blocks available as |out_length|, starting
  // at data block offset |out_start|.
  zx_status_t Next(uint32_t length, uint32_t* out_length, uint64_t* out_start);

 private:
  ExtentIterator* iterator_;
  // The latest extent pulled off of the iterator.
  const Extent* extent_ = nullptr;
  // The number of blocks left within the current extent.
  uint32_t blocks_left_ = 0;
};

// StreamBlocks is a utility function which reads up to |block_count| blocks, dumping
// continuous blocks encountered from |iterator| to the callback function |stream|.
using StreamFn = fbl::Function<zx_status_t(uint64_t local_off, uint64_t dev_off, uint32_t length)>;
zx_status_t StreamBlocks(BlockIterator* iterator, uint32_t block_count, StreamFn stream);

}  // namespace blobfs
