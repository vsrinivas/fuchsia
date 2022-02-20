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
    ZX_ASSERT(vmo_.op_range(ZX_VMO_OP_TRY_LOCK, 0, BlockSize(), nullptr, 0) == ZX_OK);
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
  Map();
  clear_flag.cancel();
  return ret;
}

zx_status_t Page::VmoOpUnlock() {
  ZX_ASSERT(IsAllocated() == true);
  ZX_ASSERT(vmo_.op_range(ZX_VMO_OP_UNLOCK, 0, BlockSize(), nullptr, 0) == ZX_OK);
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
    ZX_ASSERT(GetVnode().Vfs()->GetBc().BlockDetachVmo(std::move(vmoid_)) == ZX_OK);
    ZX_ASSERT(zx::vmar::root_self()->unmap(address_, BlockSize()) == ZX_OK);
  }
}

void Page::Map() {
  if (!SetFlag(PageFlag::kPageMapped)) {
    ZX_ASSERT(GetVnode().Vfs()->GetBc().BlockAttachVmo(vmo_, &vmoid_) == ZX_OK);
    ZX_ASSERT(zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo_, 0,
                                         BlockSize(), &address_) == ZX_OK);
  }
}

void Page::Invalidate() {
  VnodeF2fs &vnode = GetVnode();
  SuperblockInfo &superblock_info = vnode.Vfs()->GetSuperblockInfo();
  WaitOnWriteback();
  ClearUptodate();
  if (ClearDirtyForIo(false)) {
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
    ClearFlag(PageFlag::kPageWriteback);
    GetVnode().Vfs()->GetSuperblockInfo().DecreasePageCount(CountType::kWriteback);
    WakeupFlag(PageFlag::kPageWriteback);
  }
}

zx_status_t Page::VmoWrite(const void *buffer, uint64_t offset, size_t buffer_size) {
  return vmo_.write(buffer, offset, buffer_size);
}

zx_status_t Page::VmoRead(void *buffer, uint64_t offset, size_t buffer_size) {
  return vmo_.read(buffer, offset, buffer_size);
}

void Page::Zero(size_t index, size_t count) {
  if (index < capacity() && index + count <= capacity()) {
    ZX_ASSERT(vmo_.op_range(ZX_VMO_OP_ZERO, index * BlockSize(), count * BlockSize(), nullptr, 0) ==
              ZX_OK);
  }
}

FileCache::FileCache(VnodeF2fs *vnode) : vnode_(vnode) {}
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
    (*iter).Unmap();
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
    FX_LOGS(INFO) << vnode_->GetNameView() << " : " << page->GetKey() << " Page cannot be found";
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
      ZX_ASSERT(page->IsWriteback() == false);
      page->ClearUptodate();
      page.reset();
    }
  }
  return ZX_OK;
}

// TODO: Do not acquire tree_lock_ during writeback
// TODO: Reset FileCache of the meta/node vnode after checkpoint
// TODO: Consider using a global lock as below
// if (!IsDir())
//   mutex_lock(&superblock_info->writepages);
// Writeback()
// if (!IsDir())
//   mutex_unlock(&superblock_info->writepages);
// Vfs()->RemoveDirtyDirInode(this);
pgoff_t FileCache::Writeback(const WritebackOperation &operation) {
  pgoff_t written_blocks = 0;
  pgoff_t prev_key = kPgOffMax;
  while (true) {
    std::lock_guard tree_lock(tree_lock_);
    if (page_tree_.is_empty()) {
      break;
    }
    // Acquire the first Page from the the lower bound of operation.start and
    // all subsequent Pages by iterating to operation.end.
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

    if (current->IsLastReference() && current->TryLock()) {
      fbl::RefPtr<Page> page = current.CopyPointer();
      if (page->IsDirty()) {
        if (!operation.if_page || operation.if_page(page) == ZX_OK) {
          ZX_ASSERT(page->IsUptodate());
          page->Map();
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
            ++written_blocks;
            ZX_ASSERT(page->IsWriteback());
          }
        }
        page->Unlock();
      } else {
        page->Unlock();
        EvictUnsafe(page.get());
      }
    }
    if (written_blocks >= operation.to_write) {
      break;
    }
  }

  sync_completion_t completion;
  if (written_blocks) {
    if (operation.bSync) {
      vnode_->Vfs()->ScheduleWriterSubmitPages(&completion);
      ZX_ASSERT(sync_completion_wait(&completion, ZX_TIME_INFINITE) == ZX_OK);
    }
  }
  return written_blocks;
}

}  // namespace f2fs
