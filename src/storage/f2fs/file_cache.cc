// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

Page::Page(FileCache *file_cache, pgoff_t index) : file_cache_(file_cache), index_(index) {}

ino_t Page::GetVnodeId() const { return GetVnode().GetKey(); }

VnodeF2fs &Page::GetVnode() const { return file_cache_->GetVnode(); }

FileCache &Page::GetFileCache() const { return *file_cache_; }

Page::~Page() {
  ZX_ASSERT(IsAllocated() == true);
  ZX_ASSERT(IsWriteback() == false);
  ZX_ASSERT(InContainer() == false);
  ZX_ASSERT(IsDirty() == false);
  ZX_ASSERT(IsMapped() == false);
  ZX_ASSERT(IsStorageMapped() == false);
  ZX_ASSERT(IsLocked() == false);
}

void Page::fbl_recycle() {
  if (IsStorageMapped()) {
    // It failed to be uptodate due to invalid index.
    // In other cases, StorageUnmap() should be called in ClearWriteback().
    ZX_ASSERT(!IsUptodate());
    StorageUnmap();
  }
  Unmap();
  VmoOpUnlock();
  // For active Pages, we evict them with strong references, so it is safe to call InContainer()
  // without the tree lock.
  if (InContainer()) {
    file_cache_->Downgrade(this);
  } else {
    delete this;
  }
}

bool Page::SetDirty() {
  SetUptodate();
  if (!flags_[static_cast<uint8_t>(PageFlag::kPageDirty)].test_and_set(std::memory_order_acquire)) {
    ZX_ASSERT(vmo_.op_range(ZX_VMO_OP_TRY_LOCK, 0, BlockSize(), nullptr, 0) == ZX_OK);
    VnodeF2fs &vnode = GetVnode();
    SuperblockInfo &superblock_info = vnode.Vfs()->GetSuperblockInfo();
    vnode.MarkInodeDirty();
    if (vnode.IsNode()) {
      superblock_info.IncreasePageCount(CountType::kDirtyNodes);
    } else if (vnode.IsDir()) {
      superblock_info.IncreasePageCount(CountType::kDirtyDents);
      superblock_info.IncreaseDirtyDir();
      vnode.IncreaseDirtyPageCount();
    } else if (vnode.IsMeta()) {
      superblock_info.IncreasePageCount(CountType::kDirtyMeta);
      superblock_info.SetDirty();
    } else {
      superblock_info.IncreasePageCount(CountType::kDirtyData);
      vnode.IncreaseDirtyPageCount();
    }
    return false;
  }
  return true;
}

bool Page::ClearDirtyForIo(bool for_writeback) {
  ZX_ASSERT(IsLocked());
  VnodeF2fs &vnode = GetVnode();
  SuperblockInfo &superblock_info = vnode.Vfs()->GetSuperblockInfo();
  if (IsDirty()) {
    ClearFlag(PageFlag::kPageDirty);
    if (!for_writeback) {
      VmoOpUnlock();
    } else {
      StorageMap();
    }
    if (vnode.IsNode()) {
      superblock_info.DecreasePageCount(CountType::kDirtyNodes);
    } else if (vnode.IsDir()) {
      superblock_info.DecreasePageCount(CountType::kDirtyDents);
      superblock_info.DecreaseDirtyDir();
      vnode.DecreaseDirtyPageCount();
    } else if (vnode.IsMeta()) {
      superblock_info.DecreasePageCount(CountType::kDirtyMeta);
    } else {
      superblock_info.DecreasePageCount(CountType::kDirtyData);
      vnode.DecreaseDirtyPageCount();
    }
    return true;
  }
  return false;
}

zx_status_t Page::GetPage(bool need_vmo_lock) {
  zx_status_t ret = ZX_OK;
  auto clear_flag = fit::defer([&] { ClearFlag(PageFlag::kPageAlloc); });

  if (!flags_[static_cast<uint8_t>(PageFlag::kPageAlloc)].test_and_set(std::memory_order_acquire)) {
    ZX_ASSERT(IsDirty() == false);
    ZX_ASSERT(IsWriteback() == false);
    ClearFlag(PageFlag::kPageUptodate);
    if (ret = vmo_.create(BlockSize(), ZX_VMO_DISCARDABLE, &vmo_); ret != ZX_OK) {
      return ret;
    }
    if (ret = vmo_.op_range(ZX_VMO_OP_COMMIT | ZX_VMO_OP_TRY_LOCK, 0, BlockSize(), nullptr, 0);
        ret != ZX_OK) {
      return ret;
    }
  } else if (need_vmo_lock) {
    if (ret = vmo_.op_range(ZX_VMO_OP_TRY_LOCK, 0, BlockSize(), nullptr, 0); ret != ZX_OK) {
      ZX_ASSERT(ret == ZX_ERR_UNAVAILABLE);
      ZX_ASSERT(IsDirty() == false);
      ZX_ASSERT(IsWriteback() == false);
      ClearFlag(PageFlag::kPageUptodate);
      if (ret = vmo_.op_range(ZX_VMO_OP_COMMIT | ZX_VMO_OP_TRY_LOCK, 0, BlockSize(), nullptr, 0);
          ret != ZX_OK) {
        return ret;
      }
    }
  }
  if (!IsUptodate()) {
    ZX_ASSERT(!IsWriteback());
    StorageMap();
  }
  Map();
  clear_flag.cancel();
  return ret;
}

zx_status_t Page::VmoOpUnlock() {
  ZX_ASSERT(IsAllocated() == true);
  ZX_ASSERT(vmo_.op_range(ZX_VMO_OP_UNLOCK, 0, BlockSize(), nullptr, 0) == ZX_OK);
  return ZX_OK;
}

void Page::PutPage(fbl::RefPtr<Page> &&page, bool unlock) {
  ZX_ASSERT(page != nullptr);
  if (unlock) {
    page->Unlock();
  }
  page.reset();
}

void Page::StorageUnmap() {
  if (IsStorageMapped()) {
    ZX_ASSERT(GetVnode().Vfs()->GetBc().BlockDetachVmo(std::move(vmoid_)) == ZX_OK);
    ClearStorageMapped();
  }
}

void Page::StorageMap() {
  if (!SetFlag(PageFlag::kPageStorageMapped)) {
    ZX_ASSERT(GetVnode().Vfs()->GetBc().BlockAttachVmo(vmo_, &vmoid_) == ZX_OK);
  }
}
void Page::Unmap() {
  if (IsMapped()) {
    ClearMapped();
    ZX_ASSERT(zx::vmar::root_self()->unmap(address_, BlockSize()) == ZX_OK);
  }
}

void Page::Map() {
  if (!SetFlag(PageFlag::kPageMapped)) {
    ZX_ASSERT(zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo_, 0,
                                         BlockSize(), &address_) == ZX_OK);
  }
}

void Page::Invalidate() {
  WaitOnWriteback();
  bool locked = TryLock();
  ClearUptodate();
  ClearDirtyForIo(false);
  if (!locked) {
    Unlock();
  }
}

void Page::SetUptodate() {
  ZX_ASSERT(IsLocked());
  if (!SetFlag(PageFlag::kPageUptodate)) {
    StorageUnmap();
  }
}

void Page::ClearUptodate() { ClearFlag(PageFlag::kPageUptodate); }

void Page::WaitOnWriteback() {
  if (IsWriteback()) {
    GetVnode().Vfs()->ScheduleWriterSubmitPages();
  }
  WaitOnFlag(PageFlag::kPageWriteback);
}

void Page::SetWriteback() {
  if (!SetFlag(PageFlag::kPageWriteback)) {
    GetVnode().Vfs()->GetSuperblockInfo().IncreasePageCount(CountType::kWriteback);
  }
}

void Page::ClearWriteback() {
  if (IsWriteback()) {
    StorageUnmap();
    GetVnode().Vfs()->GetSuperblockInfo().DecreasePageCount(CountType::kWriteback);
    ClearFlag(PageFlag::kPageWriteback);
    WakeupFlag(PageFlag::kPageWriteback);
  }
}

zx_status_t Page::VmoWrite(const void *buffer, uint64_t offset, size_t buffer_size) {
  return vmo_.write(buffer, offset, buffer_size);
}

zx_status_t Page::VmoRead(void *buffer, uint64_t offset, size_t buffer_size) {
  return vmo_.read(buffer, offset, buffer_size);
}

zx_status_t Page::Zero(size_t index, size_t count) {
  return vmo_.op_range(ZX_VMO_OP_ZERO, index * BlockSize(), count * BlockSize(), nullptr, 0);
}

FileCache::FileCache(VnodeF2fs *vnode) : vnode_(vnode) {}
FileCache::~FileCache() {
  {
    std::lock_guard tree_lock(tree_lock_);
    CleanupPagesUnsafe();
    ZX_ASSERT(page_tree_.is_empty());
  }
}

void FileCache::Downgrade(Page *raw_page) {
  // We can downgrade multiple Pages simultaneously.
  fs::SharedLock tree_lock(tree_lock_);
  // Resurrect |this|.
  raw_page->ResurrectRef();
  fbl::RefPtr<Page> page = fbl::ImportFromRawPtr(raw_page);
  // Leak it to keep alive in FileCache.
  __UNUSED auto leak = fbl::ExportToRawPtr(&page);
  raw_page->ClearActive();
  recycle_cvar_.notify_all();
}

zx_status_t FileCache::AddPageUnsafe(const fbl::RefPtr<Page> &page) {
  if (page->InContainer()) {
    return ZX_ERR_ALREADY_EXISTS;
  }
  page_tree_.insert(page.get());
  return ZX_OK;
}

zx_status_t FileCache::GetPage(const pgoff_t index, fbl::RefPtr<Page> *out) {
  bool need_vmo_lock = true;
  {
    std::lock_guard tree_lock(tree_lock_);
    auto ret = GetPageUnsafe(index, out);
    if (ret.is_error()) {
      *out = fbl::MakeRefCounted<Page>(this, index);
      ZX_ASSERT(AddPageUnsafe(*out) == ZX_OK);
      (*out)->SetActive();
    } else {
      need_vmo_lock = ret.value();
    }
  }
  (*out)->Lock();
  (*out)->GetPage(need_vmo_lock);
  return ZX_OK;
}

zx_status_t FileCache::FindPage(const pgoff_t index, fbl::RefPtr<Page> *out) {
  zx::status<bool> ret;
  {
    std::lock_guard tree_lock(tree_lock_);
    ret = GetPageUnsafe(index, out);
    if (ret.is_error()) {
      return ret.error_value();
    }
  }
  (*out)->Lock();
  (*out)->GetPage(ret.value());
  (*out)->Unlock();
  return ZX_OK;
}

zx::status<bool> FileCache::GetPageUnsafe(const pgoff_t index, fbl::RefPtr<Page> *out) {
  while (true) {
    auto raw_ptr = page_tree_.find(index).CopyPointer();
    if (raw_ptr != nullptr) {
      if (raw_ptr->IsActive()) {
        *out = fbl::MakeRefPtrUpgradeFromRaw(raw_ptr, tree_lock_);
        // We wait for it to be resurrected in fbl_recycle().
        if (*out == nullptr) {
          recycle_cvar_.wait(tree_lock_);
          continue;
        }
        // Here, Page::ref_count should not be less than one.
        return zx::ok(false);
      }
      *out = fbl::ImportFromRawPtr(raw_ptr);
      (*out)->SetActive();
      ZX_ASSERT((*out)->IsLastReference());
      return zx::ok(true);
    }
    break;
  }
  return zx::error(ZX_ERR_NOT_FOUND);
}

zx_status_t FileCache::EvictUnsafe(Page *page) {
  if (!page->InContainer()) {
    return ZX_ERR_NOT_FOUND;
  }
  page_tree_.erase(*page);
  return ZX_OK;
}

void FileCache::CleanupPagesUnsafe(pgoff_t start, pgoff_t end, bool invalidate) {
  pgoff_t prev_key = kPgOffMax;
  while (!page_tree_.is_empty()) {
    // Acquire Pages from the the lower bound of |start| to |end|.
    auto key = (prev_key < kPgOffMax) ? prev_key : start;
    auto current = page_tree_.lower_bound(key);
    if (current == page_tree_.end() || current->GetKey() >= end) {
      break;
    }
    if (!current->IsActive()) {
      // No other reference it |current|. It is safe to release it.
      prev_key = current->GetKey();
      EvictUnsafe(&(*current));
      if (invalidate) {
        current->Invalidate();
      }
      delete &(*current);
    } else {
      auto page = fbl::MakeRefPtrUpgradeFromRaw(&(*current), tree_lock_);
      // When it is being recycled, we should wait.
      // Try again.
      if (page == nullptr) {
        recycle_cvar_.wait(tree_lock_);
        continue;
      }
      // There are some strong references. It shall be released in fbl_recycle().
      prev_key = page->GetKey();
      EvictUnsafe(page.get());
      if (invalidate) {
        page->Invalidate();
      } else {
        // Wait for it to be written.
        page->WaitOnWriteback();
      }
    }
  }
}

void FileCache::InvalidatePages(pgoff_t start, pgoff_t end) {
  std::lock_guard tree_lock(tree_lock_);
  CleanupPagesUnsafe(start, end, true);
}

void FileCache::Reset() {
  std::lock_guard tree_lock(tree_lock_);
  CleanupPagesUnsafe();
}

std::vector<fbl::RefPtr<Page>> FileCache::GetLockedDirtyPagesUnsafe(
    const WritebackOperation &operation) {
  pgoff_t prev_key = kPgOffMax;
  std::vector<fbl::RefPtr<Page>> pages;
  pgoff_t nwritten = 0;
  while (nwritten <= operation.to_write) {
    if (page_tree_.is_empty()) {
      break;
    }
    // Acquire Pages from the the lower bound of |operation.start| to |operation.end|.
    auto key = (prev_key < kPgOffMax) ? prev_key : operation.start;
    auto current = page_tree_.lower_bound(key);
    if (current == page_tree_.end() || current->GetKey() >= operation.end) {
      break;
    }
    // Unless the |prev_key| Page is evicted, we should try the next Page.
    if (prev_key == current->GetKey()) {
      ++current;
      if (current == page_tree_.end() || current->GetKey() >= operation.end) {
        break;
      }
    }
    prev_key = current->GetKey();
    auto raw_page = current.CopyPointer();
    // Do not touch active Pages.
    if (raw_page->IsActive()) {
      continue;
    }
    ZX_ASSERT(!raw_page->TryLock());
    if (raw_page->IsDirty()) {
      fbl::RefPtr<Page> page = fbl::ImportFromRawPtr(raw_page);
      ZX_ASSERT(page->IsLastReference());
      if (!operation.if_page || operation.if_page(page) == ZX_OK) {
        ZX_ASSERT(page->IsUptodate());
        page->SetActive();
        pages.push_back(std::move(page));
        ++nwritten;
      } else {
        page->Unlock();
        // It is the last reference. Just leak it and keep it alive in FileCache.
        __UNUSED auto leak = fbl::ExportToRawPtr(&page);
      }
    } else if (operation.bReleasePages || !vnode_->IsActive()) {
      // There is no other reference. It is safe to release it..
      raw_page->Unlock();
      EvictUnsafe(raw_page);
      delete raw_page;
    } else {
      raw_page->Unlock();
    }
  }
  return pages;
}

// TODO: Consider using a global lock as below
// if (!IsDir())
//   mutex_lock(&superblock_info->writepages);
// Writeback()
// if (!IsDir())
//   mutex_unlock(&superblock_info->writepages);
// Vfs()->RemoveDirtyDirInode(this);
pgoff_t FileCache::Writeback(WritebackOperation &operation) {
  std::vector<fbl::RefPtr<Page>> pages;
  {
    std::lock_guard tree_lock(tree_lock_);
    pages = GetLockedDirtyPagesUnsafe(operation);
  }

  pgoff_t nwritten = 0;
  for (size_t i = 0; i < pages.size(); ++i) {
    fbl::RefPtr<Page> page = std::move(pages[i]);
    pages[i] = nullptr;
    ZX_ASSERT(page->IsUptodate());
    ZX_ASSERT(page->IsLocked());
    // All node pages require mappings to log the next address in their footer.
    if (vnode_->IsNode()) {
      page->Map();
    }
    zx_status_t ret = page->GetVnode().WriteDirtyPage(page, false);
    ZX_ASSERT(!page->IsDirty());
    if (ret != ZX_OK) {
      if (ret != ZX_ERR_NOT_FOUND && ret != ZX_ERR_OUT_OF_RANGE) {
        // TODO: In case of failure, we just redirty it.
        page->SetDirty();
        FX_LOGS(WARNING) << "[f2fs] A unexpected error occured during writing Pages: " << ret;
      }
      page->ClearWriteback();
    } else {
      ++nwritten;
      --operation.to_write;
    }
    page->Unmap();
    page->Unlock();
  }
  if (operation.bSync) {
    sync_completion_t completion;
    vnode_->Vfs()->ScheduleWriterSubmitPages(&completion);
    ZX_ASSERT(sync_completion_wait(&completion, ZX_TIME_INFINITE) == ZX_OK);
  }
  return nwritten;
}

}  // namespace f2fs
