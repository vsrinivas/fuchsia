// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_DIR_ENTRY_CACHE_H_
#define SRC_STORAGE_F2FS_DIR_ENTRY_CACHE_H_

#ifdef __Fuchsia__
#include <fbl/slab_allocator.h>

namespace f2fs {

constexpr uint32_t kDirEntryCacheSlabSize = 65536;
constexpr uint32_t kDirEntryCacheSlabCount = 1;

// When a directory with inline dentry is converted to non-inline dentry, existing entries will be
// located to the first data page (page 0) of the directory. By using page index 0 for cached inline
// dir entries, the cached entries do not need to be changed on the conversion. Inline and
// non-inline dir entries can still be separated using InodeInfoFlag::kInlineDentry flag of parent.
constexpr pgoff_t kCachedInlineDirEntryPageIndex = 0;

class DirEntryCacheElement;

using ElementRefPtr = fbl::RefPtr<DirEntryCacheElement>;

using ElementAllocatorTraits =
    fbl::UnlockedSlabAllocatorTraits<ElementRefPtr, kDirEntryCacheSlabSize>;
using ElementAllocator = fbl::SlabAllocator<ElementAllocatorTraits>;
using ElementList = fbl::DoublyLinkedList<ElementRefPtr>;

using EntryKey = std::pair<ino_t, std::string>;

class DirEntryCacheElement : public fbl::RefCounted<DirEntryCacheElement>,
                             public fbl::SlabAllocated<ElementAllocatorTraits>,
                             public fbl::DoublyLinkedListable<ElementRefPtr> {
 public:
  DirEntryCacheElement(ino_t parent_ino, std::string_view name) : parent_ino_(parent_ino) {
    name_ = name;
  }

  ino_t GetParentIno() const { return parent_ino_; }
  std::string_view GetName() const { return name_.GetStringView(); }

  DirEntry GetDirEntry() const { return dir_entry_; }
  void SetDirEntry(DirEntry &de) { dir_entry_ = de; }

  pgoff_t GetDataPageIndex() const { return data_page_index_; }
  void SetDataPageIndex(pgoff_t data_page_index) { data_page_index_ = data_page_index; }

 private:
  ino_t parent_ino_;
  NameString name_;
  DirEntry dir_entry_;
  pgoff_t data_page_index_ = 0;
};

class DirEntryCache {
 public:
  DirEntryCache();

  DirEntryCache(const DirEntryCache &) = delete;
  DirEntryCache &operator=(const DirEntryCache &) = delete;
  DirEntryCache(DirEntryCache &&) = delete;
  DirEntryCache &operator=(DirEntryCache &&) = delete;

  ~DirEntryCache();

  // It is called on unmount, and it deallocates entire elements in |map_| and |element_lru_list_|.
  // Mounted filesystem can be remounted without recreating F2fs and DirEntryCache instances.
  // Therefore, explicit deallocation on unmount is needed.
  void Reset() __TA_EXCLUDES(lock_);

  zx::result<DirEntry> LookupDirEntry(ino_t parent_ino, std::string_view child_name)
      __TA_EXCLUDES(lock_);
  zx::result<pgoff_t> LookupDataPageIndex(ino_t parent_ino, std::string_view child_name)
      __TA_EXCLUDES(lock_);
  void UpdateDirEntry(ino_t parent_ino, std::string_view child_name, DirEntry &dir_entry,
                      pgoff_t data_page_index) __TA_EXCLUDES(lock_);
  void RemoveDirEntry(ino_t parent_ino, std::string_view child_name) __TA_EXCLUDES(lock_);

  // For testing
  bool IsElementInCache(ino_t parent_ino, std::string_view child_name) const __TA_EXCLUDES(lock_);
  bool IsElementAtHead(ino_t parent_ino, std::string_view child_name) const __TA_EXCLUDES(lock_);
  const std::map<EntryKey, ElementRefPtr> &GetMap() const __TA_EXCLUDES(lock_);

 private:
  DirEntryCacheElement &AllocateElement(ino_t parent_ino, std::string_view child_name)
      __TA_REQUIRES(lock_);
  void DeallocateElement(ElementRefPtr element) __TA_REQUIRES(lock_);

  void AddNewDirEntry(ino_t parent_ino, std::string_view child_name, DirEntry &dir_entry,
                      pgoff_t data_page_index) __TA_REQUIRES(lock_);

  ElementRefPtr FindElementRefPtr(ino_t parent_ino, std::string_view child_name) const
      __TA_REQUIRES(lock_);
  DirEntryCacheElement *FindElement(ino_t parent_ino, std::string_view child_name)
      __TA_REQUIRES(lock_);

  void OnCacheHit(ElementRefPtr &element) __TA_REQUIRES(lock_);
  void Evict() __TA_REQUIRES(lock_);

  static EntryKey GenerateKey(ino_t parent_ino, std::string_view child_name) {
    return EntryKey(parent_ino, std::string(child_name));
  }

  std::unique_ptr<ElementAllocator> slab_allocator_ __TA_GUARDED(lock_);
  std::map<EntryKey, ElementRefPtr> map_ __TA_GUARDED(lock_);
  ElementList element_lru_list_ __TA_GUARDED(lock_);
  // Since LRU list needs modification even for lookup, using mutex rather than shared mutex
  mutable std::mutex lock_;
};

}  // namespace f2fs

#endif  // __Fuchsia__

#endif  // SRC_STORAGE_F2FS_DIR_ENTRY_CACHE_H_
