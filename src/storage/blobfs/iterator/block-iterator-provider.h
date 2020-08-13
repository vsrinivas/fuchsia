// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_ITERATOR_BLOCK_ITERATOR_PROVIDER_H_
#define SRC_STORAGE_BLOBFS_ITERATOR_BLOCK_ITERATOR_PROVIDER_H_

#include <memory>

#include "block-iterator.h"

namespace blobfs {

// Interface for a class that provides instances of `BlockIterator` by node index.
class BlockIteratorProvider {
 public:
  // Provide a valid `BlockIterator` for the block at node index `node_index`.
  virtual BlockIterator BlockIteratorByNodeIndex(uint32_t node_index) = 0;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_ITERATOR_BLOCK_ITERATOR_PROVIDER_H_
