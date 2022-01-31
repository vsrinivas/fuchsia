// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

ino_t Page::GetVnodeId() const { return vnode_->GetKey(); }

void Page::fbl_recycle() {
  ZX_ASSERT(IsAllocated() == true);
  ZX_ASSERT(IsLocked() == false);
  // TODO: it can be true when async io is available.
  ZX_ASSERT(IsWriteback() == false);
  if (IsMapped()) {
    ClearFlag(PageFlag::kPageMapped);
    ZX_ASSERT(zx::vmar::root_self()->unmap(address_, kPageSize) == ZX_OK);
  }
  if (IsDirty()) {
    ZX_ASSERT(IsUptodate());
    // the tree should maintain it for further wb.
  }
  delete this;
}

bool Page::SetDirty() {
  SetUptodate();
  if (!flags_[static_cast<uint8_t>(PageFlag::kPageDirty)].test_and_set(std::memory_order_acquire)) {
    ZX_ASSERT(vmo_.op_range(ZX_VMO_OP_TRY_LOCK, 0, kPageSize, nullptr, 0) == ZX_OK);
    SuperblockInfo &superblock_info = vnode_->Vfs()->GetSuperblockInfo();
    vnode_->MarkInodeDirty();
    if (vnode_->IsNode()) {
      superblock_info.IncreasePageCount(CountType::kDirtyNodes);
    } else if (vnode_->IsDir()) {
      superblock_info.IncreasePageCount(CountType::kDirtyDents);
      superblock_info.IncreaseDirtyDir();
      vnode_->IncreaseDirtyDentries();
    } else if (vnode_->IsMeta()) {
      superblock_info.IncreasePageCount(CountType::kDirtyMeta);
      superblock_info.SetDirty();
    }
    return false;
  }
  return true;
}

zx_status_t Page::GetPage() {
  zx_status_t ret = ZX_OK;
  auto clear_flag = fit::defer([&] { ClearFlag(PageFlag::kPageAlloc); });

  if (!flags_[static_cast<uint8_t>(PageFlag::kPageAlloc)].test_and_set(std::memory_order_acquire)) {
    ZX_ASSERT(IsDirty() == false);
    ZX_ASSERT(IsWriteback() == false);
    ClearFlag(PageFlag::kPageUptodate);
    if (ret = vmo_.create(kPageSize, ZX_VMO_DISCARDABLE, &vmo_); ret != ZX_OK) {
      return ret;
    }
    if (ret = vmo_.op_range(ZX_VMO_OP_COMMIT | ZX_VMO_OP_TRY_LOCK, 0, kPageSize, nullptr, 0);
        ret != ZX_OK) {
      return ret;
    }
  } else {
    if (ret = vmo_.op_range(ZX_VMO_OP_TRY_LOCK, 0, kPageSize, nullptr, 0); ret != ZX_OK) {
      ZX_ASSERT(ret == ZX_ERR_UNAVAILABLE);
      ZX_ASSERT(IsDirty() == false);
      ZX_ASSERT(IsWriteback() == false);
      ClearFlag(PageFlag::kPageUptodate);
      if (ret = vmo_.op_range(ZX_VMO_OP_COMMIT | ZX_VMO_OP_TRY_LOCK, 0, kPageSize, nullptr, 0);
          ret != ZX_OK) {
        return ret;
      }
    }
  }
  if (!flags_[static_cast<uint8_t>(PageFlag::kPageMapped)].test_and_set(
          std::memory_order_acquire)) {
    if (ret = zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo_, 0, kPageSize,
                                         &address_);
        ret != ZX_OK) {
      ZX_ASSERT(vmo_.op_range(ZX_VMO_OP_UNLOCK | ZX_VMO_OP_DECOMMIT, 0, kPageSize, nullptr, 0) ==
                ZX_OK);
      return ret;
    }
  }
  clear_flag.cancel();
  return ret;
}

zx_status_t Page::PutPage(bool unmap) {
  ZX_ASSERT(IsAllocated() == true);
  ZX_ASSERT(vmo_.op_range(ZX_VMO_OP_UNLOCK, 0, kPageSize, nullptr, 0) == ZX_OK);
  if (unmap)
    ZX_ASSERT(zx::vmar::root_self()->unmap(address_, kPageSize) == ZX_OK);
  return ZX_OK;
}

}  // namespace f2fs
