// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_FILE_CACHE_H_
#define SRC_STORAGE_F2FS_FILE_CACHE_H_

#include <condition_variable>
#include <utility>

#include <fbl/intrusive_wavl_tree.h>
#include <safemath/checked_math.h>
#include <storage/buffer/block_buffer.h>

#include "src/storage/f2fs/f2fs_types.h"
#include "src/storage/f2fs/vmo_manager.h"

namespace f2fs {

class F2fs;
class VnodeF2fs;
class FileCache;

enum class PageFlag {
  kPageUptodate = 0,  // It is uptodate. No need to read blocks from disk.
  kPageDirty,         // It needs to be written out.
  kPageWriteback,     // It is under writeback.
  kPageLocked,        // It is locked. Wait for it to be unlocked.
  kPageVmoLocked,     // Its vmo is locked to prevent mm from reclaiming it.
  kPageMapped,        // It has a valid mapping to the address space.
  kPageActive,        // It is being referenced.
  // TODO: Clear |kPageMmapped| when all mmaped areas are unmapped.
  kPageMmapped,   // It is mmapped. Once set, it remains regardless of munmap.
  kPageColdData,  // It is under garbage collecting. It must not be inplace updated.
  kPageFlagSize,
};

constexpr pgoff_t kPgOffMax = std::numeric_limits<pgoff_t>::max();

// It defines a writeback operation.
struct WritebackOperation {
  pgoff_t start = 0;  // All dirty Pages within the range of [start, end) are subject to writeback.
  pgoff_t end = kPgOffMax;
  pgoff_t to_write = kPgOffMax;  // The number of dirty Pages to be written.
  bool bSync = false;            // If true, FileCache::Writeback() waits for writeback Pages to be
                                 // written to disk.
  bool bReleasePages =
      true;  // If true, it releases clean Pages while traversing FileCache::page_tree_.
  bool bReclaim = false;             // If true, it is invoked for memory reclaim.
  VnodeCallback if_vnode = nullptr;  // If set, it determines which vnodes are subject to writeback.
  PageCallback if_page = nullptr;    // If set, it determines which Pages are subject to writeback.
  NodePageCallback node_page_cb = nullptr;  // If set, the callback is executed. This callback is
                                            // for node page only and is executed before writeback.
};

template <typename T, bool EnableAdoptionValidator = ZX_DEBUG_ASSERT_IMPLEMENTED>
class PageRefCounted : public fs::VnodeRefCounted<T> {
 public:
  PageRefCounted(const Page &) = delete;
  PageRefCounted &operator=(const PageRefCounted &) = delete;
  PageRefCounted(const PageRefCounted &&) = delete;
  PageRefCounted &operator=(const PageRefCounted &&) = delete;
  using ::fbl::internal::RefCountedBase<EnableAdoptionValidator>::IsLastReference;

 protected:
  constexpr PageRefCounted() = default;
  ~PageRefCounted() = default;
};

class Page : public PageRefCounted<Page>,
             public fbl::Recyclable<Page>,
             public fbl::WAVLTreeContainable<Page *>,
             public fbl::DoublyLinkedListable<fbl::RefPtr<Page>> {
 public:
  Page() = delete;
  Page(FileCache *file_cache, pgoff_t index);
  Page(const Page &) = delete;
  Page &operator=(const Page &) = delete;
  Page(const Page &&) = delete;
  Page &operator=(const Page &&) = delete;
  virtual ~Page();

  void fbl_recycle() { RecyclePage(); }

  pgoff_t GetKey() const { return index_; }
  pgoff_t GetIndex() const { return GetKey(); }
  VnodeF2fs &GetVnode() const;
  FileCache &GetFileCache() const;
  // A caller is allowed to access |this| via address_ after GetPage().
  // Calling it ensures that VmoManager creates and maintains a vmo called VmoNode that
  // |this| will use. When VmoManager does not have the corresponding VmoNode, it creates
  // a discardable vmo and tracks a reference count to the vmo.
  // The vmo keeps VMO_OP_LOCK as long as any corresponding RefPtr<Page> exists. The mapping
  // also keeps with its vmo.
  zx_status_t GetPage();
  zx_status_t VmoOpUnlock(bool evict = false);
  zx::result<bool> VmoOpLock();
  template <typename T = void>
  T *GetAddress() const {
    ZX_DEBUG_ASSERT(IsMapped());
    return reinterpret_cast<T *>(address_);
  }

  bool IsUptodate() const { return TestFlag(PageFlag::kPageUptodate); }
  bool IsDirty() const { return TestFlag(PageFlag::kPageDirty); }
  bool IsWriteback() const { return TestFlag(PageFlag::kPageWriteback); }
  bool IsLocked() const { return TestFlag(PageFlag::kPageLocked); }
  bool IsVmoLocked() const { return TestFlag(PageFlag::kPageVmoLocked); }
  bool IsMapped() const { return TestFlag(PageFlag::kPageMapped); }
  bool IsActive() const { return TestFlag(PageFlag::kPageActive); }
  bool IsMmapped() const { return TestFlag(PageFlag::kPageMmapped); }
  bool IsColdData() const { return TestFlag(PageFlag::kPageColdData); }

  void ClearMapped() { ClearFlag(PageFlag::kPageMapped); }

  // Each Setxxx() method atomically sets a flag and returns the previous value.
  // It is called when the first reference is made.
  bool SetActive() { return SetFlag(PageFlag::kPageActive); }
  // It is called after the last reference is destroyed in FileCache::Downgrade().
  void ClearActive() { ClearFlag(PageFlag::kPageActive); }

  void Lock() {
    while (flags_[static_cast<uint8_t>(PageFlag::kPageLocked)].test_and_set(
        std::memory_order_acquire)) {
      flags_[static_cast<uint8_t>(PageFlag::kPageLocked)].wait(true, std::memory_order_relaxed);
    }
  }
  bool TryLock() {
    return flags_[static_cast<uint8_t>(PageFlag::kPageLocked)].test_and_set(
        std::memory_order_acquire);
  }
  void Unlock() {
    if (IsLocked()) {
      ClearFlag(PageFlag::kPageLocked);
      WakeupFlag(PageFlag::kPageLocked);
    }
  }

  // It ensures that |this| is written to disk if IsDirty() is true.
  void WaitOnWriteback();
  bool SetWriteback();
  void ClearWriteback();

  bool SetUptodate();
  void ClearUptodate();

  // Set its dirty flag and increase the corresponding count of its type.
  bool SetDirty();
  bool ClearDirtyForIo();

  // It ensures that the contents of |this| is synchronized with the corresponding pager backed vmo.
  void SetMmapped();
  bool ClearMmapped();

  void SetColdData();
  bool ClearColdData();

  // It invalidates |this| for truncate and punch-a-hole operations.
  // It clears PageFlag::kPageUptodate and PageFlag::kPageDirty. If a caller invalidates
  // |this| that is under writeback, writeback keeps going. So, it is recommended to invalidate
  // its block address in a dnode or nat entry first.
  void Invalidate();

  void ZeroUserSegment(uint64_t start, uint64_t end) const {
    if (start < end && end <= BlockSize()) {
      std::memset(GetAddress<uint8_t>() + start, 0, end - start);
    }
  }

  static uint32_t BlockSize() { return kPageSize; }
  block_t GetBlockAddr() const { return block_addr_; }
  zx::result<> SetBlockAddr(block_t addr);

  // Check that |this| Page exists in FileCache.
  bool InTreeContainer() const { return fbl::WAVLTreeContainable<Page *>::InContainer(); }
  // Check that |this| Page exists in any PageList.
  bool InListContainer() const {
    return fbl::DoublyLinkedListable<fbl::RefPtr<Page>>::InContainer();
  }

  F2fs *fs() const;

 protected:
  // It notifies VmoManager that there is no reference to |this|.
  void RecyclePage();

 private:
  zx_status_t Map();
  void WaitOnFlag(PageFlag flag) {
    while (flags_[static_cast<uint8_t>(flag)].test(std::memory_order_acquire)) {
      flags_[static_cast<uint8_t>(flag)].wait(true, std::memory_order_relaxed);
    }
  }
  bool TestFlag(PageFlag flag) const {
    return flags_[static_cast<uint8_t>(flag)].test(std::memory_order_acquire);
  }
  void ClearFlag(PageFlag flag) {
    flags_[static_cast<uint8_t>(flag)].clear(std::memory_order_relaxed);
  }
  void WakeupFlag(PageFlag flag) {
    if (flag == PageFlag::kPageLocked) {
      flags_[static_cast<uint8_t>(flag)].notify_one();
    } else {
      flags_[static_cast<uint8_t>(flag)].notify_all();
    }
  }
  bool SetFlag(PageFlag flag) {
    return flags_[static_cast<uint8_t>(flag)].test_and_set(std::memory_order_acquire);
  }

  // After a successful call to GetPage(), it has a valid mapping and virtual address
  // through which a user can access to the vmo. It is valid only when IsMapped() returns true.
  zx_vaddr_t address_ = 0;
  // It is used to track the status of a page by using PageFlag
  std::array<std::atomic_flag, static_cast<uint8_t>(PageFlag::kPageFlagSize)> flags_ = {
      ATOMIC_FLAG_INIT};
#ifndef __Fuchsia__
  FsBlock blk_;
#endif  // __Fuchsia__
  // It indicates FileCache to which |this| belongs.
  FileCache *file_cache_ = nullptr;
  // It is used as the key of |this| in a lookup table (i.e., FileCache::page_tree_).
  // It indicates different information according to the type of FileCache::vnode_ such as file,
  // node, and meta vnodes. For file vnodes, it has file offset. For node vnodes, it indicates the
  // node id. For meta vnode, it points to the block address to which the metadata is written.
  const pgoff_t index_;
  block_t block_addr_ = kNullAddr;
};

// LockedPage is a wrapper class for f2fs::Page lock management.
// When LockedPage holds "fbl::RefPtr<Page> page" and the page is not nullptr, it guarantees that
// the page is locked.
//
// The syntax looks something like...
// fbl::RefPtr<Page> unlocked_page;
// {
//   LockedPage locked_page(unlocked_page);
//   do something requiring page lock...
// }
//
// When Page is used as a function parameter, you should use `Page&` type for unlocked page, and use
// `LockedPage&` type for locked page.
class LockedPage final {
 public:
  LockedPage() : page_(nullptr) {}

  LockedPage(const LockedPage &) = delete;
  LockedPage &operator=(const LockedPage &) = delete;

  LockedPage(LockedPage &&p) noexcept {
    page_ = std::move(p.page_);
    p.page_ = nullptr;
  }
  LockedPage &operator=(LockedPage &&p) noexcept {
    reset();
    page_ = std::move(p.page_);
    p.page_ = nullptr;
    return *this;
  }

  explicit LockedPage(fbl::RefPtr<Page> page, bool try_lock = true) {
    page_ = std::move(page);
    if (try_lock) {
      page_->Lock();
    }
    ZX_ASSERT(page_->IsLocked());
  }

  ~LockedPage() { reset(); }

  void reset() {
    if (page_ != nullptr) {
      ZX_DEBUG_ASSERT(page_->IsLocked());
      page_->Unlock();
      page_.reset();
    }
  }

  // Call Page::SetDirty().
  // If |add_to_list| is true, it is inserted into F2fs::dirty_data_page_list_.
  bool SetDirty(bool add_to_list = true);

  // release() returns the unlocked page without changing its ref_count.
  // After release() is called, the LockedPage instance no longer has the ownership of the Page.
  // Therefore, the LockedPage instance should no longer be referenced.
  fbl::RefPtr<Page> release(bool unlock = true) {
    if (page_ != nullptr && unlock) {
      page_->Unlock();
    }
    return fbl::RefPtr<Page>(std::move(page_));
  }

  // CopyRefPtr() returns copied RefPtr, so that increases ref_count of page.
  // The page remains locked, and still managed by the LockedPage instance.
  fbl::RefPtr<Page> CopyRefPtr() { return page_; }

  template <typename T = Page>
  T &GetPage() {
    return static_cast<T &>(*page_);
  }

  Page *get() { return page_.get(); }
  Page &operator*() { return *page_; }
  Page *operator->() { return page_.get(); }
  explicit operator bool() const { return page_ != nullptr; }

  // Comparison against nullptr operators (of the form, myptr == nullptr).
  bool operator==(decltype(nullptr)) const { return (page_ == nullptr); }
  bool operator!=(decltype(nullptr)) const { return (page_ != nullptr); }

 private:
  fbl::RefPtr<Page> page_ = nullptr;
};

class FileCache {
 public:
#ifdef __Fuchsia__
  FileCache(VnodeF2fs *vnode, VmoManager *vmo_manager);
#else   // __Fuchsia__
  FileCache(VnodeF2fs *vnode);
#endif  // __Fuchsia__
  FileCache() = delete;
  FileCache(const FileCache &) = delete;
  FileCache &operator=(const FileCache &) = delete;
  FileCache(const FileCache &&) = delete;
  FileCache &operator=(const FileCache &&) = delete;
  ~FileCache();

  // It returns a locked Page corresponding to |index| from |page_tree_|.
  // If there is no Page, it creates and returns a locked Page.
  zx_status_t GetPage(pgoff_t index, LockedPage *out) __TA_EXCLUDES(tree_lock_);
  // It returns locked pages corresponding to |page_offsets| from |page_tree_|.
  // If kInvalidPageOffset is included in |page_offsets|, the corresponding Page will be a null
  // page.
  // If there is no corresponding Page in |page_tree_|, it creates a new Page.
  zx::result<std::vector<LockedPage>> GetPages(const std::vector<pgoff_t> &page_offsets)
      __TA_EXCLUDES(tree_lock_);
  // It returns locked Pages corresponding to [start - end) from |page_tree_|.
  zx::result<std::vector<LockedPage>> GetPages(pgoff_t start, pgoff_t end)
      __TA_EXCLUDES(tree_lock_);
  // It returns locked Pages corresponding to [start - end) from |page_tree_|.
  // If there is no corresponding Page, the returned page will be a null page.
  zx::result<std::vector<LockedPage>> FindPages(pgoff_t start, pgoff_t end)
      __TA_EXCLUDES(tree_lock_);
  LockedPage GetNewPage(pgoff_t index) __TA_REQUIRES(tree_lock_);
  // It returns an unlocked Page corresponding to |index| from |page_tree|.
  // If it fails to find the Page in |page_tree_|, it returns ZX_ERR_NOT_FOUND.
  zx_status_t FindPage(pgoff_t index, fbl::RefPtr<Page> *out) __TA_EXCLUDES(tree_lock_);
  // It tries to write out dirty Pages that meets |operation| in |page_tree_|.
  pgoff_t Writeback(WritebackOperation &operation) __TA_EXCLUDES(tree_lock_);
  // It invalidates Pages within the range of |start| to |end| in |page_tree_|.
  std::vector<LockedPage> InvalidatePages(pgoff_t start, pgoff_t end) __TA_EXCLUDES(tree_lock_);
  // It removes all Pages from |page_tree_|. It should be called when no one can get access to
  // |vnode_|. (e.g., fbl_recycle()) It assumes that all active Pages are under writeback.
  void Reset() __TA_EXCLUDES(tree_lock_);
  void ClearDirtyPages(pgoff_t start, pgoff_t end) __TA_EXCLUDES(tree_lock_);
  VnodeF2fs &GetVnode() const { return *vnode_; }
  // Only Page::RecyclePage() is allowed to call it.
  void Downgrade(Page *raw_page) __TA_EXCLUDES(tree_lock_);
  bool IsOrphan() { return is_orphan_.test(std::memory_order_relaxed); }
  bool SetOrphan() { return is_orphan_.test_and_set(std::memory_order_relaxed); }
  F2fs *fs() const;
#ifdef __Fuchsia__
  VmoManager &GetVmoManager() { return *vmo_manager_; }
#endif  // __Fuchsia__

 private:
  // If |page| is unlocked, it returns a locked |page|. If |page| is already locked,
  // it returns ZX_ERR_UNAVAILABLE after waiting for |page| to be unlocked. While waiting,
  // |tree_lock_| keeps unlocked to prevent a deadlock problem that would occur when two threads
  // try to call FileCache::GetPage() for Pages in a duplicate range. When the locked
  // |page| is unlocked, it acquires |tree_lock_| again and returns ZX_ERR_UNAVAILABLE since
  // it is not allowed to acquire |tree_lock_| with |page| locked. Then, a caller may retry it
  // with the same |page|.
  zx::result<LockedPage> GetLockedPage(fbl::RefPtr<Page> page) __TA_REQUIRES(tree_lock_);
  // It returns a set of locked dirty Pages that meet |operation|.
  std::vector<LockedPage> GetLockedDirtyPagesUnsafe(const WritebackOperation &operation)
      __TA_REQUIRES(tree_lock_);
  zx::result<LockedPage> GetLockedPageFromRawUnsafe(Page *raw_page) __TA_REQUIRES(tree_lock_);
  zx::result<LockedPage> GetPageUnsafe(const pgoff_t index) __TA_REQUIRES(tree_lock_);
  zx_status_t AddPageUnsafe(const fbl::RefPtr<Page> &page) __TA_REQUIRES(tree_lock_);
  zx_status_t EvictUnsafe(Page *page) __TA_REQUIRES(tree_lock_);
  // It returns all Pages from |page_tree_| within the range of |start| to |end|.
  // If there is no corresponding Page in page_tree_, the Page will not be included in the returned
  // vector. Therefore, returned vector's size could be smaller than |end - start|.
  std::vector<LockedPage> GetLockedPagesUnsafe(pgoff_t start = 0, pgoff_t end = kPgOffMax)
      __TA_REQUIRES(tree_lock_);
  // It returns all Pages from |page_tree_| corresponds to |page_offsets|.
  // If there is no corresponding Page in page_tree_ or if page_offset is kInvalidPageOffset,
  // the corresponding page will be null LockedPage in the returned vector.
  // Therefore, returned vector's size is same as |page_offsets.size()|.
  std::vector<LockedPage> GetLockedPagesUnsafe(const std::vector<pgoff_t> &page_offsets)
      __TA_REQUIRES(tree_lock_);
  // It evicts all Pages within the range of |start| to |end| and returns them locked.
  // When a caller resets returned Pages after doing some necessary work, they will be deleted.
  std::vector<LockedPage> CleanupPagesUnsafe(pgoff_t start = 0, pgoff_t end = kPgOffMax)
      __TA_REQUIRES(tree_lock_);

  using PageTreeTraits = fbl::DefaultKeyedObjectTraits<pgoff_t, Page>;
  using PageTree = fbl::WAVLTree<pgoff_t, Page *, PageTreeTraits>;

  fs::SharedMutex tree_lock_;
  // If its file is orphaned, set it to prevent further dirty Pages.
  std::atomic_flag is_orphan_ = ATOMIC_FLAG_INIT;
  std::condition_variable_any recycle_cvar_;
  PageTree page_tree_ __TA_GUARDED(tree_lock_);
  VnodeF2fs *vnode_;
#ifdef __Fuchsia__
  VmoManager *vmo_manager_;
#endif  // __Fuchsia__
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_FILE_CACHE_H_
