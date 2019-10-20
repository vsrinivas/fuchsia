// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vector-extent-iterator.h"

#include <stdint.h>
#include <zircon/types.h>

#include <blobfs/format.h>
#include <fbl/vector.h>

namespace blobfs {

VectorExtentIterator::VectorExtentIterator(const fbl::Vector<ReservedExtent>& extents)
    : extents_(extents) {}

bool VectorExtentIterator::Done() const { return extent_index_ == extents_.size(); }

zx_status_t VectorExtentIterator::Next(const Extent** out) {
  ZX_DEBUG_ASSERT(!Done());
  block_count_ += extents_[extent_index_].extent().Length();
  *out = &extents_[extent_index_].extent();

  extent_index_++;
  return ZX_OK;
}

uint64_t VectorExtentIterator::BlockIndex() const { return block_count_; }

}  // namespace blobfs
