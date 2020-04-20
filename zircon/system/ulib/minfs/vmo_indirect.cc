// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minfs_private.h"
#include "vmo_indirect.h"

namespace minfs {

zx_status_t VmoIndirect::Init(VnodeMinfs* vnode) {
  if (vmo_ != nullptr) {
    return ZX_OK;
  }

  vmo_ = fzl::ResizeableVmoMapper::Create(GetVmoSizeForDoublyIndirect(), "minfs-indirect");

  zx_status_t status =
      vnode->Vfs()->GetMutableBcache()->device()->BlockAttachVmo(vmo_->vmo(), &vmoid_);
  if (status != ZX_OK) {
    vmo_.reset();
    return status;
  }

  // Load initial set of indirect blocks
  if ((status = LoadIndirectBlocks(vnode, vnode->GetInode()->inum, kMinfsIndirect, 0)) != ZX_OK) {
    vmo_.reset();
    return status;
  }

  // Load doubly indirect blocks
  if ((status = LoadIndirectBlocks(vnode, vnode->GetInode()->dinum, kMinfsDoublyIndirect,
                                   GetVmoOffsetForDoublyIndirect(0)) != ZX_OK)) {
    vmo_.reset();
    return status;
  }

  return ZX_OK;
}

zx_status_t VmoIndirect::LoadIndirectBlocks(VnodeMinfs* vnode, const blk_t* iarray, uint32_t count,
                                            uint32_t block) {
  zx_status_t status;
  if ((status = Init(vnode)) != ZX_OK) {
    return status;
  }

  // It's not safe to grow the VMO here because |iarray| might be a pointer to something within the
  // VMO (e.g. a double indirect block pointer).
  ValidateVmoSize(vmo_->vmo().get(), block + count - 1);

  fs::BufferedOperationsBuilder builder;
  for (uint32_t i = 0; i < count; i++) {
    const blk_t ibno = iarray[i];
    if (ibno != 0) {
      vnode->Vfs()->ValidateBno(ibno);
      fs::internal::BorrowedBuffer buffer(vmoid_.get());
      builder.Add(storage::Operation{.type = storage::OperationType::kRead,
                                     .vmo_offset = block + i,
                                     .dev_offset = ibno + vnode->Vfs()->Info().dat_block,
                                     .length = 1},
                  &buffer);
    }
  }
  return vnode->Vfs()->GetMutableBcache()->RunRequests(builder.TakeOperations());
}

zx_status_t VmoIndirect::LoadIndirectWithinDoublyIndirect(VnodeMinfs* vnode, uint32_t dindex) {
  size_t size = GetVmoSizeForIndirect(dindex);
  if (vmo_->size() >= size) {
    // We've already loaded this indirect (within dind) block.
    return ZX_OK;
  } else {
    zx_status_t status = vmo_->Grow(size);
    if (status != ZX_OK) {
      return status;
    }
  }

  blk_t* dientry = GetBlocks(GetVmoOffsetForDoublyIndirect(dindex));
  return LoadIndirectBlocks(vnode, dientry, kMinfsDirectPerIndirect,
                            GetVmoOffsetForIndirect(dindex));
}

void VmoIndirect::ClearBlock(uint32_t block) {
  ZX_DEBUG_ASSERT(IsValid());
  uintptr_t addr = reinterpret_cast<uintptr_t>(vmo_->start());
  ValidateVmoSize(vmo_->vmo().get(), block);
  memset(reinterpret_cast<void*>(addr + kMinfsBlockSize * block), 0, kMinfsBlockSize);
}

}  // namespace minfs
