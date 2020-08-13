// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_ITERATOR_VECTOR_EXTENT_ITERATOR_H_
#define SRC_STORAGE_BLOBFS_ITERATOR_VECTOR_EXTENT_ITERATOR_H_

#include <stdbool.h>
#include <stdint.h>
#include <zircon/types.h>

#include <blobfs/format.h>
#include <fbl/vector.h>

#include "allocator/extent-reserver.h"
#include "extent-iterator.h"

namespace blobfs {

// Allows traversing a collection of extents from a not-yet allocated node.
//
// This iterator is useful for accessing blocks of blobs which have not yet been
// committed to disk.
class VectorExtentIterator : public ExtentIterator {
 public:
  VectorExtentIterator(const fbl::Vector<ReservedExtent>& extents);
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(VectorExtentIterator);

  ////////////////
  // ExtentIterator interface.

  bool Done() const final;
  zx_status_t Next(const Extent** out) final;
  uint64_t BlockIndex() const final;

 private:
  const fbl::Vector<ReservedExtent>& extents_;
  size_t extent_index_ = 0;
  uint64_t block_count_ = 0;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_ITERATOR_VECTOR_EXTENT_ITERATOR_H_
