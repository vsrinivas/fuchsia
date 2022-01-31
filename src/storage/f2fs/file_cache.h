// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_FILE_CACHE_H_
#define SRC_STORAGE_F2FS_FILE_CACHE_H_

namespace f2fs {

class VnodeF2fs;

enum class PageFlag {
  kPageUptodate = 0,
  kPageDirty,
  kPageWriteback,
  kPageLocked,
  kPageAlloc,
  kPageMapped,
  kPageFlagSize = 8,
};

class Page : public fbl::RefCounted<Page>,
             public fbl::Recyclable<Page>,
             public fbl::WAVLTreeContainable<fbl::RefPtr<Page>> {
 public:
  Page() = delete;
  Page(VnodeF2fs *vnode, pgoff_t index) : vnode_(vnode), index_(index) {}
  Page(const Page &) = delete;
  Page &operator=(const Page &) = delete;
  Page(const Page &&) = delete;
  Page &operator=(const Page &&) = delete;

  void fbl_recycle();

  ino_t GetVnodeId() const;
  pgoff_t GetKey() const { return index_; }
  VnodeF2fs *GetVnode() const { return vnode_; }
  pgoff_t GetIndex() const { return index_; }
  zx_status_t GetPage();
  zx_status_t PutPage(bool unmap);
  void *GetAddress() const {
    // TODO: vaddr need to be atomically mapped on demand
    ZX_ASSERT(TestFlag(PageFlag::kPageMapped));
    return (void *)address_;
  }

  bool IsUptodate() const {
    return flags_[static_cast<uint8_t>(PageFlag::kPageUptodate)].test(std::memory_order_relaxed);
  }
  bool IsDirty() const {
    return flags_[static_cast<uint8_t>(PageFlag::kPageDirty)].test(std::memory_order_relaxed);
  }
  bool IsWriteback() const {
    return flags_[static_cast<uint8_t>(PageFlag::kPageWriteback)].test(std::memory_order_relaxed);
  }
  bool IsLocked() const {
    return flags_[static_cast<uint8_t>(PageFlag::kPageLocked)].test(std::memory_order_relaxed);
  }
  bool IsAllocated() const {
    return flags_[static_cast<uint8_t>(PageFlag::kPageAlloc)].test(std::memory_order_relaxed);
  }
  bool IsMapped() const {
    return flags_[static_cast<uint8_t>(PageFlag::kPageMapped)].test(std::memory_order_relaxed);
  }

  void Lock() {
    while (flags_[static_cast<uint8_t>(PageFlag::kPageLocked)].test_and_set(
        std::memory_order_acquire)) {
      flags_[static_cast<uint8_t>(PageFlag::kPageLocked)].wait(true, std::memory_order_relaxed);
    }
  }
  void Unlock() {
    ClearFlag(PageFlag::kPageLocked);
    WakeupFlag(PageFlag::kPageLocked);
  }
  void WaitOnWriteback() { WaitOnFlag(PageFlag::kPageWriteback); }
  void SetWriteback() {
    ZX_ASSERT(vmo_.op_range(ZX_VMO_OP_TRY_LOCK, 0, kPageSize, nullptr, 0) == ZX_OK);
    SetFlag(PageFlag::kPageWriteback);
  }
  void ClearWriteback() {
    ClearFlag(PageFlag::kPageWriteback);
    WakeupFlag(PageFlag::kPageWriteback);
    ZX_ASSERT(vmo_.op_range(ZX_VMO_OP_UNLOCK, 0, kPageSize, nullptr, 0) == ZX_OK);
  }
  void SetUptodate() { SetFlag(PageFlag::kPageUptodate); }
  void ClearUptodate() { ClearFlag(PageFlag::kPageUptodate); }
  bool SetDirty();
  bool ClearDirtyForIo() {
    if (IsDirty()) {
      ClearFlag(PageFlag::kPageDirty);
      ZX_ASSERT(vmo_.op_range(ZX_VMO_OP_UNLOCK, 0, kPageSize, nullptr, 0) == ZX_OK);
      return true;
    }
    return false;
  }
  void ZeroUserSegment(uint32_t start, uint32_t end) {
    ZX_ASSERT(end <= kPageSize && start < end);
    if (end > start) {
      memset(reinterpret_cast<uint8_t *>(address_) + start, 0, end - start);
    }
  }
  static void PutPage(fbl::RefPtr<Page> &&page, int unlock) {
    if (!page) {
      return;
    }
    page->PutPage(false);
    if (unlock) {
      page->Unlock();
    }
    page.reset();
  }

 private:
  void WaitOnFlag(PageFlag flag) {
    while (flags_[static_cast<uint8_t>(flag)].test(std::memory_order_relaxed)) {
      flags_[static_cast<uint8_t>(flag)].wait(true, std::memory_order_relaxed);
    }
  }
  bool TestFlag(PageFlag flag) const {
    return flags_[static_cast<uint8_t>(flag)].test(std::memory_order_relaxed);
  }
  void ClearFlag(PageFlag flag) {
    flags_[static_cast<uint8_t>(flag)].clear(std::memory_order_release);
  }
  void WakeupFlag(PageFlag flag) { flags_[static_cast<uint8_t>(flag)].notify_all(); }
  bool SetFlag(PageFlag flag) {
    return flags_[static_cast<uint8_t>(flag)].test_and_set(std::memory_order_acquire);
  }

  zx_vaddr_t address_ = 0;
  std::array<std::atomic_flag, static_cast<uint8_t>(PageFlag::kPageFlagSize)> flags_ = {
      ATOMIC_FLAG_INIT};
  zx::vmo vmo_;
  VnodeF2fs *vnode_ = nullptr;
  pgoff_t index_ = -1;
};

class FileCache {
 public:
  FileCache(VnodeF2fs *vnode) : vnode_(vnode) {}
  FileCache() = delete;
  FileCache(const FileCache &) = delete;
  FileCache &operator=(const FileCache &) = delete;
  FileCache(const FileCache &&) = delete;
  FileCache &operator=(const FileCache &&) = delete;

 private:
  using PageTreeTraits = fbl::DefaultKeyedObjectTraits<pgoff_t, Page>;
  using PageTree = fbl::WAVLTree<pgoff_t, fbl::RefPtr<Page>, PageTreeTraits>;

  fs::SharedMutex tree_lock_;
  PageTree page_tree_ __TA_GUARDED(tree_lock_);
  __UNUSED VnodeF2fs *vnode_{nullptr};
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_FILE_CACHE_H_
