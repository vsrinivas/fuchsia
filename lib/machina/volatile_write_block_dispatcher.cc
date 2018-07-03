// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/volatile_write_block_dispatcher.h"

#include <fbl/auto_lock.h>
#include <lib/zx/vmar.h>

#include "garnet/lib/machina/bits.h"
#include "lib/fxl/logging.h"

namespace machina {

zx_status_t VolatileWriteBlockDispatcher::Create(
    fbl::unique_ptr<BlockDispatcher> dispatcher,
    fbl::unique_ptr<BlockDispatcher>* out) {
  zx::vmo vmo;
  size_t size = dispatcher->size();
  zx_status_t status = zx::vmo::create(size, ZX_VMO_NON_RESIZABLE, &vmo);
  if (status != ZX_OK) {
    return status;
  }

  const char* vmo_name = "volatile-block";
  status = vmo.set_property(ZX_PROP_NAME, vmo_name, strlen(vmo_name));
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to set VMO name";
  }

  uintptr_t map_address;
  status = zx::vmar::root_self().map(
      0, vmo, 0, size, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
      &map_address);
  if (status != ZX_OK) {
    return status;
  }

  *out = fbl::unique_ptr<VolatileWriteBlockDispatcher>(
      new VolatileWriteBlockDispatcher(fbl::move(dispatcher), fbl::move(vmo),
                                       map_address, size));
  return ZX_OK;
}

VolatileWriteBlockDispatcher::VolatileWriteBlockDispatcher(
    fbl::unique_ptr<BlockDispatcher> dispatcher, zx::vmo vmo,
    uintptr_t map_address, size_t vmo_size)
    : BlockDispatcher(dispatcher->size(), false /* read-only */),
      dispatcher_(fbl::move(dispatcher)),
      vmo_(fbl::move(vmo)),
      vmo_addr_(map_address),
      vmo_size_(vmo_size) {}

VolatileWriteBlockDispatcher::~VolatileWriteBlockDispatcher() {
  FXL_CHECK(ZX_OK == zx::vmar::root_self().unmap(vmo_addr_, vmo_size_));
}

zx_status_t VolatileWriteBlockDispatcher::Flush() {
  return dispatcher_->Flush();
}

zx_status_t VolatileWriteBlockDispatcher::Submit() {
  return dispatcher_->Submit();
}

zx_status_t VolatileWriteBlockDispatcher::Read(off_t disk_offset, void* buf,
                                               size_t size) {
  if (!ValidateBlockParams(disk_offset, size)) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AutoLock lock(&mutex_);
  uint8_t* dest = static_cast<uint8_t*>(buf);
  while (size > 0) {
    size_t block = disk_offset / kBlockSize;
    size_t num_blocks = size / kBlockSize;
    size_t first_unallocated_block;
    bitmap_.Get(block, block + num_blocks, &first_unallocated_block);
    if (first_unallocated_block == block) {
      // Not Allocated, delegate to dispatcher.
      zx_status_t status = dispatcher_->Read(disk_offset, dest, kBlockSize);
      if (status != ZX_OK) {
        return status;
      }
      disk_offset += kBlockSize;
      dest += kBlockSize;
      size -= kBlockSize;
    } else {
      // Region is at least partially cached.
      size_t cached_size = (first_unallocated_block - block) * kBlockSize;
      memcpy(dest, reinterpret_cast<void*>(vmo_addr_ + disk_offset),
             cached_size);
      disk_offset += cached_size;
      dest += cached_size;

      FXL_DCHECK(size >= cached_size);
      size = (cached_size > size) ? 0 : (size - cached_size);
    }
  }

  return ZX_OK;
}

zx_status_t VolatileWriteBlockDispatcher::Write(off_t disk_offset,
                                                const void* buf, size_t size) {
  if (!ValidateBlockParams(disk_offset, size)) {
    return ZX_ERR_INVALID_ARGS;
  }

  size_t block = disk_offset / kBlockSize;
  size_t num_blocks = size / kBlockSize;

  fbl::AutoLock lock(&mutex_);
  zx_status_t status = bitmap_.Set(block, block + num_blocks);
  if (status != ZX_OK) {
    return status;
  }
  memcpy(reinterpret_cast<uint8_t*>(vmo_addr_ + disk_offset), buf, size);
  return ZX_OK;
}

bool VolatileWriteBlockDispatcher::ValidateBlockParams(off_t disk_offset,
                                                       size_t size) {
  return is_aligned(disk_offset, kBlockSize) && is_aligned(size, kBlockSize);
}

}  // namespace machina
