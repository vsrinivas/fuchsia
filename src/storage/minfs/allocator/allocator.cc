// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/allocator/allocator.h"

#include <stdlib.h>
#include <string.h>

#include <limits>
#include <utility>

#include <bitmap/raw-bitmap.h>
#include <storage/buffer/block_buffer.h>

namespace minfs {

namespace {

// Trivial BlockBuffer that doesn't own the underlying buffer.
// TODO(fxbug.dev/47947): Remove this.
class UnownedBuffer : public storage::BlockBuffer {
 public:
  UnownedBuffer(vmoid_t vmoid) : vmoid_(vmoid) {}
  ~UnownedBuffer() {}

  // BlockBuffer interface:
  size_t capacity() const final { return 0; }
  uint32_t BlockSize() const final { return 0; }
  vmoid_t vmoid() const final { return vmoid_; }
  zx_handle_t Vmo() const final { return ZX_HANDLE_INVALID; }
  void* Data(size_t index) final { return nullptr; }
  const void* Data(size_t index) const final { return nullptr; }

 private:
  vmoid_t vmoid_;
};

}  // namespace

Allocator::~Allocator() {
  std::scoped_lock lock(lock_);
  ZX_ASSERT(pending_changes_.empty());
}

zx_status_t Allocator::LoadStorage(fs::BufferedOperationsBuilder* builder) {
  std::scoped_lock lock(lock_);
  storage::OwnedVmoid vmoid;
  zx_status_t status = storage_->AttachVmo(map_.StorageUnsafe()->GetVmo(), &vmoid);
  if (status != ZX_OK) {
    return status;
  }
  UnownedBuffer buffer(vmoid.get());
  builder->AddVmoid(std::move(vmoid));
  storage_->Load(builder, &buffer);
  return ZX_OK;
}

size_t Allocator::GetAvailableLocked() const {
  size_t total_reserved = reserved_;
  for (const PendingChange* change : pending_changes_) {
    total_reserved += change->GetReservedCount();
  }
  ZX_DEBUG_ASSERT(storage_->PoolAvailable() >= total_reserved);
  return storage_->PoolAvailable() - total_reserved;
}

WriteData Allocator::GetMapDataLocked() { return map_.StorageUnsafe()->GetVmo().get(); }

fbl::Vector<BlockRegion> Allocator::GetAllocatedRegions() const {
  std::scoped_lock lock(lock_);
  fbl::Vector<BlockRegion> out_regions;
  uint64_t offset = 0;
  uint64_t end = 0;
  while (!map_.Scan(end, map_.size(), false, &offset)) {
    if (map_.Scan(offset, map_.size(), true, &end)) {
      end = map_.size();
    }
    out_regions.push_back({offset, end - offset});
  }
  return out_regions;
}

}  // namespace minfs
