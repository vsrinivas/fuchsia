// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

Page::Page(FileCache *file_cache, pgoff_t index) : file_cache_(file_cache), index_(index) {}

ino_t Page::GetVnodeId() const { return GetVnode().GetKey(); }

VnodeF2fs &Page::GetVnode() const { return file_cache_->GetVnode(); }

FileCache &Page::GetFileCache() const { return *file_cache_; }

void Page::fbl_recycle() {
  ZX_ASSERT(IsAllocated() == true);
  ZX_ASSERT(IsLocked() == false);
  // TODO: it can be true when async io is available.
  ZX_ASSERT(IsWriteback() == false);
  ZX_ASSERT(InContainer() == false);
  ZX_ASSERT(IsDirty() == false);
  if (IsMapped()) {
    Unmap();
  }
  delete this;
}

bool Page::SetDirty() {
  SetUptodate();
  if (!flags_[static_cast<uint8_t>(PageFlag::kPageDirty)].test_and_set(std::memory_order_acquire)) {
    ZX_ASSERT(vmo_.op_range(ZX_VMO_OP_TRY_LOCK, 0, kPageSize, nullptr, 0) == ZX_OK);
    VnodeF2fs &vnode = GetVnode();
    SuperblockInfo &superblock_info = vnode.Vfs()->GetSuperblockInfo();
    vnode.MarkInodeDirty();
    if (vnode.IsNode()) {
      superblock_info.IncreasePageCount(CountType::kDirtyNodes);
    } else if (vnode.IsDir()) {
      superblock_info.IncreasePageCount(CountType::kDirtyDents);
      superblock_info.IncreaseDirtyDir();
      vnode.IncreaseDirtyDentries();
    } else if (vnode.IsMeta()) {
      superblock_info.IncreasePageCount(CountType::kDirtyMeta);
      superblock_info.SetDirty();
    } else {
      superblock_info.IncreasePageCount(CountType::kDirtyData);
    }
    return false;
  }
  return true;
}

zx_status_t Page::GetPage(bool need_vmo_lock) {
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
  } else if (need_vmo_lock) {
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

zx_status_t Page::VmoOpUnlock() {
  ZX_ASSERT(IsAllocated() == true);
  ZX_ASSERT(vmo_.op_range(ZX_VMO_OP_UNLOCK, 0, kPageSize, nullptr, 0) == ZX_OK);
  return ZX_OK;
}

void Page::PutPage(fbl::RefPtr<Page> &&page, int unlock) {
  ZX_ASSERT(page != nullptr);
  pgoff_t index = page->GetKey();
  FileCache &file_cache = page->GetFileCache();
  if (unlock) {
    page->Unlock();
  }
  page.reset();
  file_cache.UnmapAndReleasePage(index);
}

void Page::Unmap() {
  if (IsMapped()) {
    ClearMapped();
    ZX_ASSERT(zx::vmar::root_self()->unmap(address_, kPageSize) == ZX_OK);
  }
}

void Page::Invalidate() {
  VnodeF2fs &vnode = GetVnode();
  SuperblockInfo &superblock_info = vnode.Vfs()->GetSuperblockInfo();
  ClearUptodate();
  if (ClearDirtyForIo()) {
    if (vnode.IsNode()) {
      superblock_info.DecreasePageCount(CountType::kDirtyNodes);
    } else if (vnode.IsDir()) {
      superblock_info.DecreasePageCount(CountType::kDirtyDents);
      superblock_info.DecreaseDirtyDir();
      vnode.DecreaseDirtyDentries();
    } else if (vnode.IsMeta()) {
      superblock_info.DecreasePageCount(CountType::kDirtyMeta);
    } else {
      superblock_info.DecreasePageCount(CountType::kDirtyData);
    }
    Unmap();
  }
}

FileCache::~FileCache() {
  {
    std::lock_guard tree_lock(tree_lock_);
    ResetUnsafe();
    ZX_ASSERT(page_tree_.is_empty());
  }
}

void FileCache::UnmapAndReleasePage(const pgoff_t index) {
  std::lock_guard tree_lock(tree_lock_);
  auto iter = page_tree_.find(index);
  if (iter != page_tree_.end() && (*iter).IsLastReference()) {
    if (!(*iter).IsDirty()) {
      (*iter).Unmap();
    }
    (*iter).VmoOpUnlock();
    if (!(*iter).IsUptodate()) {
      EvictUnsafe(&(*iter));
    }
  }
}

zx_status_t FileCache::AddPageUnsafe(fbl::RefPtr<Page> page) {
  if ((*page).InContainer()) {
    return ZX_ERR_ALREADY_EXISTS;
  }
  page_tree_.insert(std::move(page));
  return ZX_OK;
}

zx_status_t FileCache::GetPage(const pgoff_t index, fbl::RefPtr<Page> *out) {
  {
    std::lock_guard tree_lock(tree_lock_);
    auto ret = GetPageUnsafe(index, out);
    bool need_vmo_lock = true;
    if (ret.is_error()) {
      *out = fbl::MakeRefCounted<Page>(this, index);
      ZX_ASSERT(AddPageUnsafe(*out) == ZX_OK);
    } else {
      need_vmo_lock = ret.value();
    }
    (*out)->GetPage(need_vmo_lock);
  }
  (*out)->Lock();
  return ZX_OK;
}

zx_status_t FileCache::FindPage(const pgoff_t index, fbl::RefPtr<Page> *out) {
  std::lock_guard tree_lock(tree_lock_);
  auto ret = GetPageUnsafe(index, out);
  if (ret.is_error()) {
    return ret.error_value();
  }
  (*out)->GetPage(ret.value());
  return ZX_OK;
}

zx::status<bool> FileCache::GetPageUnsafe(const pgoff_t index, fbl::RefPtr<Page> *out) {
  auto iter = page_tree_.find(index);
  if (iter != page_tree_.end()) {
    bool is_last = (*iter).IsLastReference();
    *out = iter.CopyPointer();
    return zx::ok(is_last);
  }
  return zx::error(ZX_ERR_NOT_FOUND);
}

zx_status_t FileCache::EvictUnsafe(Page *page) {
  if (!(*page).InContainer()) {
    FX_LOGS(INFO) << "EvictUnsafe: " << vnode_->GetNameView() << "(" << vnode_->GetKey()
                  << "): " << page->GetKey() << " page cannot be found in Page tree";
    return ZX_ERR_NOT_FOUND;
  }
  fbl::RefPtr<Page> del = page_tree_.erase(*page);
  return ZX_OK;
}

void FileCache::InvalidateAllPages() {
  std::lock_guard tree_lock(tree_lock_);
  while (!page_tree_.is_empty()) {
    fbl::RefPtr<Page> page = page_tree_.pop_front();
    if (page) {
      page->Invalidate();
      page.reset();
    }
  }
}

zx_status_t FileCache::Reset() {
  std::lock_guard tree_lock(tree_lock_);
  return ResetUnsafe();
}

zx_status_t FileCache::ResetUnsafe() {
  while (!page_tree_.is_empty()) {
    fbl::RefPtr<Page> page = page_tree_.pop_front();
    if (page) {
      ZX_ASSERT(page->IsDirty() == false);
      page->ClearUptodate();
      page.reset();
    }
  }
  return ZX_OK;
}

// TODO: Do not acquire tree_lock_ during writeback
uint64_t FileCache::Writeback(const pgoff_t start, const pgoff_t end) {
  std::lock_guard tree_lock(tree_lock_);
  F2fs *fs = vnode_->Vfs();
  uint64_t written_blocks = 0;

  for (auto iter = page_tree_.lower_bound(start);
       iter != page_tree_.end() && (*iter).GetKey() < end; ++iter) {
    if ((*iter).IsDirty()) {
      ZX_ASSERT((*iter).IsUptodate());
      if (vnode_->GetKey() == fs->GetSuperblockInfo().GetNodeIno()) {
        if (zx_status_t ret = fs->GetNodeManager().F2fsWriteNodePage((*iter), false);
            ret != ZX_OK) {
          // TODO: Retry it with a newly allocated block once SSR/LFS is enabled.
          ZX_ASSERT(0);
          return ret;
        }
      } else if (vnode_->GetKey() == fs->GetSuperblockInfo().GetMetaIno()) {
        if (zx_status_t ret = fs->F2fsWriteMetaPage((*iter), false); ret != ZX_OK) {
          // TODO: Retry it with the same block. If it fails again, set kNeedCp.
          ZX_ASSERT(0);
          return ret;
        }
      } else {
        if (zx_status_t ret = vnode_->WriteDataPage(&(*iter), false); ret != ZX_OK) {
          // TODO: Retry it with a newly allocated block once SSR/LFS is enabled.
          ZX_ASSERT(0);
          return ret;
        }
      }
      ++written_blocks;
      ZX_ASSERT(!(*iter).IsDirty());
      ZX_ASSERT((*iter).IsMapped());
      if ((*iter).IsLastReference()) {
        (*iter).Unmap();
      }
    }
  }
  // TODO: Reset FileCache of the meta/node vnode after checkpoint
  return written_blocks;
}

}  // namespace f2fs
