// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_FILE_CACHE_H_
#define SRC_STORAGE_F2FS_FILE_CACHE_H_

namespace f2fs {

class VnodeF2fs;
class FileCache;

enum class PageFlag {
  kPageUptodate = 0,  // It is uptodate. No need to read blocks from disk.
  kPageDirty,         // It needs to be written out.
  kPageWriteback,     // It is under writeback.
  kPageLocked,        // It is locked. Wait for it to be unlocked.
  kPageAlloc,         // It has a valid Page::vmo_.
  kPageMapped,        // It has a valid virtual address mapped to Page::vmo_.
  kPageReferenced,    // TODO: One or more references to Page
  kPageFlagSize = 8,
};

constexpr pgoff_t kPgOffMax = std::numeric_limits<pgoff_t>::max();
// TODO: Once f2fs can get hints about memory pressure, remove it.
// Now, the maximum allowable memory for dirty data pages is 200MiB
constexpr int kMaxDirtyDataPages = 51200;

class Page : public fbl::RefCounted<Page>,
             public fbl::Recyclable<Page>,
             public fbl::WAVLTreeContainable<fbl::RefPtr<Page>> {
 public:
  Page() = delete;
  Page(FileCache *file_cache, pgoff_t index);
  Page(const Page &) = delete;
  Page &operator=(const Page &) = delete;
  Page(const Page &&) = delete;
  Page &operator=(const Page &&) = delete;

  void fbl_recycle();

  ino_t GetVnodeId() const;
  pgoff_t GetKey() const { return index_; }
  VnodeF2fs &GetVnode() const;
  pgoff_t GetIndex() const { return index_; }
  FileCache &GetFileCache() const;
  // To get a Page, f2fs should call FileCache::GetPage() or FileCache::FindPage() that
  // internally calls Page::GetPage(). It allocates a discardable |vmo_| and commits a page
  // if IsAllocated() is false. Then, it creates a mapping for |vmo_| to allow the access to
  // |vmo_| using a virtual address if IsMapped() is false. Finally, it requests ZX_VMO_OP_TRY_LOCK
  // to prevent the page of |vmo_| from being decommitted until there are one or more references. If
  // it fails, it means the the kernel has decommitted the page of |vmo_| due to memory pressure,
  // and thus it commits a page to |vmo_| and requests ZX_VMO_OP_TRY_LOCK again.
  // TODO: call op_range() w/ ZX_VMO_OP_TRYLOCK only when a caller is the first reference to |this|.
  // This way reduces the number of calls to the syscall.
  zx_status_t GetPage();
  // f2fs should call Page::PutPage() after using a Page.
  // First, it requests ZX_VMO_OP_UNLOCK to allow the kernel to free the committed page when there
  // is no reference. Then, it clears the PageFlag::kPageLoced flag and wakes up waiters if |unlock|
  // is true. A caller should set |unlock| to true if it has locked |this| before.
  // Finally, it resets the reference pointer, and then unmaps |address_| when there is no
  // reference to it except that PageFlag::kPageDirty is set. Writeback will use the mapping of a
  // dirty page soon.
  // TODO: call op_range() w/ ZX_VMO_OP_UNLOCK only when a caller is the last reference to |this|.
  // This way reduces the number of calls to the syscall.
  static void PutPage(fbl::RefPtr<Page> &&page, int unlock);
  zx_status_t VmoOpUnlock();
  void *GetAddress() const {
    // TODO: |address_| needs to be atomically mapped in a on-demand manner.
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
  void Unmap();

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
  void ClearReferenced() { ClearFlag(PageFlag::kPageReferenced); }
  void SetReferenced() { SetFlag(PageFlag::kPageReferenced); }
  void ClearMapped() { ClearFlag(PageFlag::kPageMapped); }
  // Truncate or punch-a-hole operations call it to invalidate a Page.
  // It clears PageFlag::kPageUptodate and unmaps |address_|.
  // If the Page is dirty, it clears PageFlag::kPageDirty, decreases the regarding dirty page count,
  // and unmaps |address_|.
  void Invalidate();

  void ZeroUserSegment(uint32_t start, uint32_t end) {
    ZX_ASSERT(end <= kPageSize && start < end);
    if (end > start) {
      memset(reinterpret_cast<uint8_t *>(address_) + start, 0, end - start);
    }
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

  // A virtual address mapped to |vmo_|. It is valid only when IsMapped() returns true.
  // It is unmapped when there is no reference to a clean page (not dirty)
  zx_vaddr_t address_ = 0;
  // It is used to track the status of a page by using PageFlag
  std::array<std::atomic_flag, static_cast<uint8_t>(PageFlag::kPageFlagSize)> flags_ = {
      ATOMIC_FLAG_INIT};
  // It contains the data of the block at |index_|.
  // TODO: when resizeable paged_vmo is available, clone a part of paged_vmo
  zx::vmo vmo_;
  // It indicates FileCache to which |this| belongs.
  FileCache *file_cache_ = nullptr;
  // It is used as the key of |this| in a lookup table (i.e., FileCache::page_tree_).
  // It indicates different information according to the type of FileCache::vnode_ such as file,
  // node, and meta vnodes. For file vnodes, it has file offset. For node vnodes, it indicates the
  // node id. For meta vnode, it points to the block address to which the metadata is written.
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
  ~FileCache();

  using Callback = fit::function<zx_status_t(fbl::RefPtr<Page> &)>;

  // It unmaps a Page with |index|. If kPageUptodate is not set, it remove it from the lookup
  // |page_tree_|.
  void UnmapAndReleasePage(const pgoff_t index) __TA_EXCLUDES(tree_lock_);
  // f2fs should call FileCache::GetPage() or FileCache::FindPage() to get a Page for a vnode.
  // It returns a locked Page with |index| from the lookup |page_tree_|.
  // If there is no corresponding Page in |page_tree_|, it returns a locked Page after creating and
  // inserting it into |page_tree_|.
  zx_status_t GetPage(const pgoff_t index, fbl::RefPtr<Page> *out) __TA_EXCLUDES(tree_lock_);
  // It does the same things as GetPage() except that it returns a unlocked Page.
  zx_status_t FindPage(const pgoff_t index, fbl::RefPtr<Page> *out) __TA_EXCLUDES(tree_lock_);
  zx_status_t Evict(Page *page) __TA_EXCLUDES(tree_lock_);
  uint64_t Writeback(const pgoff_t start = 0, const pgoff_t end = kPgOffMax)
      __TA_EXCLUDES(tree_lock_);
  // It removes and invalidates all Pages in |page_tree_|.
  void InvalidateAllPages() __TA_EXCLUDES(tree_lock_);
  zx_status_t Reset() __TA_EXCLUDES(tree_lock_);
  VnodeF2fs &GetVnode() const { return *vnode_; }

 private:
  zx_status_t GetPageUnsafe(const pgoff_t index, fbl::RefPtr<Page> *out) __TA_REQUIRES(tree_lock_);
  zx_status_t AddPageUnsafe(fbl::RefPtr<Page> page) __TA_REQUIRES(tree_lock_);
  zx_status_t EvictUnsafe(Page *page) __TA_REQUIRES(tree_lock_);
  zx_status_t ResetUnsafe() __TA_REQUIRES(tree_lock_);

  using PageTreeTraits = fbl::DefaultKeyedObjectTraits<pgoff_t, Page>;
  using PageTree = fbl::WAVLTree<pgoff_t, fbl::RefPtr<Page>, PageTreeTraits>;

  fs::SharedMutex tree_lock_;
  PageTree page_tree_ __TA_GUARDED(tree_lock_);
  VnodeF2fs *vnode_ = nullptr;
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_FILE_CACHE_H_
