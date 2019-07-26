// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "allocator.h"

#include <stdlib.h>
#include <string.h>

#include <limits>
#include <utility>

#include <bitmap/raw-bitmap.h>
#include <minfs/block-txn.h>

namespace minfs {

Allocator::~Allocator() {}

zx_status_t Allocator::LoadStorage(fs::ReadTxn* txn) {
  storage_->Load(txn, GetMapDataLocked());
  return ZX_OK;
}

size_t Allocator::GetAvailableLocked() const {
  ZX_DEBUG_ASSERT(storage_->PoolAvailable() >= reserved_);
  return storage_->PoolAvailable() - reserved_;
}

WriteData Allocator::GetMapDataLocked() const { return map_.StorageUnsafe()->GetData(); }

size_t Allocator::FindLocked() const {
  ZX_DEBUG_ASSERT(reserved_ > 0);
  size_t start = first_free_;

  while (true) {
    // Search for first free element in the map.
    size_t index;
    ZX_ASSERT(map_.Find(false, start, map_.size(), 1, &index) == ZX_OK);

    return index;
  }
}

}  // namespace minfs
