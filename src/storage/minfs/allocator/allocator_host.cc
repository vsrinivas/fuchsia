// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>

#include <limits>
#include <utility>

#include <bitmap/raw-bitmap.h>
#include <storage/buffer/block_buffer.h>

#include "src/storage/minfs/allocator/allocator.h"

namespace minfs {

Allocator::~Allocator() {}

zx_status_t Allocator::LoadStorage(fs::BufferedOperationsBuilder* builder) {
  fs::internal::BorrowedBuffer buffer(GetMapDataLocked());
  storage_->Load(builder, &buffer);
  return ZX_OK;
}

size_t Allocator::GetAvailableLocked() const {
  ZX_DEBUG_ASSERT(storage_->PoolAvailable() >= reserved_);
  return storage_->PoolAvailable() - reserved_;
}

WriteData Allocator::GetMapDataLocked() { return map_.StorageUnsafe()->GetData(); }

}  // namespace minfs
