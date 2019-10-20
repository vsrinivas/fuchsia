// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "allocator/extent-reserver.h"

#include <stdint.h>
#include <zircon/types.h>

#include <bitmap/rle-bitmap.h>
#include <blobfs/format.h>

namespace blobfs {

void ExtentReserver::Reserve(const Extent& extent) {
  ZX_DEBUG_ASSERT_MSG(extent.Length() > 0, "Reserving empty extents is disallowed");
  zx_status_t status = reserved_blocks_.Set(extent.Start(), extent.Start() + extent.Length());
  ZX_DEBUG_ASSERT(status == ZX_OK);
}

void ExtentReserver::Unreserve(const Extent& extent) {
  // Ensure the blocks are already reserved.
  size_t blkno_out;
  ZX_DEBUG_ASSERT(reserved_blocks_.Find(true, extent.Start(), extent.Start() + extent.Length(),
                                        extent.Length(), &blkno_out) == ZX_OK);

  zx_status_t status = reserved_blocks_.Clear(extent.Start(), extent.Start() + extent.Length());
  ZX_DEBUG_ASSERT(status == ZX_OK);
}

uint64_t ExtentReserver::ReservedBlockCount() const { return reserved_blocks_.num_bits(); }

ReservedExtent::ReservedExtent(ExtentReserver* reserver, Extent extent)
    : reserver_(reserver), extent_(extent) {
  reserver_->Reserve(extent_);
}

ReservedExtent::~ReservedExtent() { Reset(); }

ReservedExtent::ReservedExtent(ReservedExtent&& o) : reserver_(o.reserver_), extent_(o.extent_) {
  o.Release();
}

ReservedExtent& ReservedExtent::operator=(ReservedExtent&& o) {
  Reset();
  reserver_ = o.reserver_;
  extent_ = o.extent_;
  o.Release();
  return *this;
}

const Extent& ReservedExtent::extent() const {
  ZX_DEBUG_ASSERT_MSG(Reserved(), "Accessing unreserved extent");
  return extent_;
}

ReservedExtent ReservedExtent::SplitAt(BlockCountType block_split) {
  ZX_DEBUG_ASSERT_MSG(Reserved(), "Accessing unreserved extent");
  ZX_DEBUG_ASSERT(block_split < extent_.Length());
  Extent latter(extent_.Start() + block_split,
                static_cast<BlockCountType>(extent_.Length() - block_split));

  extent_.SetLength(block_split);
  return ReservedExtent(reserver_, std::move(latter));
}

void ReservedExtent::Reset() {
  if (Reserved()) {
    reserver_->Unreserve(extent());
  }
  Release();
}

void ReservedExtent::Release() { reserver_ = nullptr; }

bool ReservedExtent::Reserved() const { return reserver_ != nullptr; }

}  // namespace blobfs
