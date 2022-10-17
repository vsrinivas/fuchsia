// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/iterator/vector_extent_iterator.h"

#include <stdint.h>
#include <zircon/types.h>

#include <vector>

#include "src/storage/blobfs/format.h"

namespace blobfs {

VectorExtentIterator::VectorExtentIterator(const std::vector<ReservedExtent>& extents)
    : extents_(extents) {}

bool VectorExtentIterator::Done() const { return extent_index_ == extents_.size(); }

zx::result<const Extent*> VectorExtentIterator::Next() {
  ZX_DEBUG_ASSERT(!Done());
  const Extent& extent = extents_[extent_index_].extent();
  block_count_ += extent.Length();
  ++extent_index_;
  return zx::ok(&extent);
}

uint64_t VectorExtentIterator::BlockIndex() const { return block_count_; }

}  // namespace blobfs
