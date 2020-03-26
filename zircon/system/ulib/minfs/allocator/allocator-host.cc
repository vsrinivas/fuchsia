// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>

#include <limits>
#include <utility>

#include <bitmap/raw-bitmap.h>
#include <storage/buffer/block_buffer.h>

#include "allocator.h"

namespace minfs {

namespace {

// Trivial BlockBuffer that doesn't own the underlying buffer.
// TODO(47947): Remove this.
class UnownedBuffer : public storage::BlockBuffer {
 public:
  UnownedBuffer(const void* data) : data_(reinterpret_cast<const char*>(data)) {}
  ~UnownedBuffer() {}

  // BlockBuffer interface:
  size_t capacity() const final { return 0; }
  uint32_t BlockSize() const final { return 0; }
  vmoid_t vmoid() const final { return 0; }
  zx_handle_t Vmo() const final { return ZX_HANDLE_INVALID; }
  void* Data(size_t index) final {
    return const_cast<void*>(const_cast<const UnownedBuffer*>(this)->Data(index));
  }
  const void* Data(size_t index) const final { return data_ + index * kMinfsBlockSize; }

 private:
  const char* data_;
};

}  // namespace

Allocator::~Allocator() {}

zx_status_t Allocator::LoadStorage(fs::BufferedOperationsBuilder* builder) {
  UnownedBuffer buffer(GetMapDataLocked());
  storage_->Load(builder, &buffer);
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
