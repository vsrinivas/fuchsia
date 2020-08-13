// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_ITERATOR_BLOCK_ITERATOR_H_
#define SRC_STORAGE_BLOBFS_ITERATOR_BLOCK_ITERATOR_H_

#include <stdbool.h>
#include <stdint.h>
#include <zircon/types.h>

#include <memory>

#include <blobfs/format.h>
#include <fbl/function.h>
#include <fs/trace.h>

#include "allocated-extent-iterator.h"
#include "extent-iterator.h"

namespace blobfs {

// Wraps an ExtentIterator to allow traversal of a node in block-order rather
// than extent-order.
class BlockIterator {
 public:
  explicit BlockIterator(std::unique_ptr<ExtentIterator> iterator);
  BlockIterator(const BlockIterator&) = delete;
  BlockIterator& operator=(const BlockIterator&) = delete;

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
  std::unique_ptr<ExtentIterator> iterator_;
  // The latest extent pulled off of the iterator.
  const Extent* extent_ = nullptr;
  // The number of blocks left within the current extent.
  uint32_t blocks_left_ = 0;
};

// StreamBlocks is a utility function which reads up to |block_count| blocks, dumping
// continuous blocks encountered from |iterator| to the callback function |stream|.
using StreamFn = fbl::Function<zx_status_t(uint64_t local_off, uint64_t dev_off, uint32_t length)>;
zx_status_t StreamBlocks(BlockIterator* iterator, uint32_t block_count, StreamFn stream);

// IterateToBlock is a utility function which moves the iterator to block number |block_num|.
// Used by the blobfs pager to navigate to an arbitrary offset within a blob.
// NOTE: This can only move the iterator forward relative to the current position.
zx_status_t IterateToBlock(BlockIterator* iter, uint32_t block_num);

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_ITERATOR_BLOCK_ITERATOR_H_
