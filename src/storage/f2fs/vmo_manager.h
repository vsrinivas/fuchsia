// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_VMO_MANAGER_H_
#define SRC_STORAGE_F2FS_VMO_MANAGER_H_

namespace f2fs {

// It manages the lifecycle of |vmo_| that Pages use in each vnode.
class VmoNode : public fbl::WAVLTreeContainable<std::unique_ptr<VmoNode>> {
 public:
  VmoNode() = delete;
  VmoNode(const VmoNode &) = delete;
  VmoNode &operator=(const VmoNode &) = delete;
  VmoNode(const VmoNode &&) = delete;
  VmoNode &operator=(const VmoNode &&) = delete;
  constexpr VmoNode(pgoff_t index) : index_(index) {}
  ~VmoNode();

  // It ensures that |vmo_| keeps VMO_OP_LOCK as long as any Pages refer to it
  // by calling Page::GetPage(). When it needs to reuse |vmo_| that it unlocked due to no
  // reference to |vmo_|, it tries VMO_OP_TRY_LOCK to check if kernel has reclaimed any pages
  // of |vmo_|. If so, it does VMO_OP_LOCK to check which pages were decommitted by kernel.
  zx::result<bool> CreateAndLockVmo(pgoff_t offset);
  // It unlocks |vmo_| when there is no Page using it.
  zx_status_t UnlockVmo(pgoff_t offset);
  zx::result<zx_vaddr_t> GetAddress(pgoff_t offset);
  pgoff_t GetKey() const { return index_; }
  uint64_t GetActivePages() const { return active_pages_; }

 private:
  // It indicates the size of |VmoNode::vmo_| in kPageSize units.
  // Currently, it is set to the f2fs segment size.
  static constexpr size_t kVmoSize = kDefaultBlocksPerSegment;

  zx_vaddr_t PageIndexToAddress(pgoff_t page_index);
  pgoff_t AddressToPageIndex(zx_vaddr_t address);

  // It tracks which Page has been decommitted by kernel during |vmo_| unlocked.
  // When a bit is 0, a caller (i.e., Page::GetPage()) clears the kUptodate flag of the
  // corresponding Page and fill the Page with data read from disk.
  std::bitset<kVmoSize> page_bitmap_;
  zx::vmo vmo_;
  // A mapping to |vmo_|. It keeps until VmoNode is deleted.
  zx_vaddr_t address_ = 0;
  // The number of Pages refering to |vmo_|.
  uint64_t active_pages_ = 0;
  const pgoff_t index_;
};

// It maintains VmoNodes in a WAVL tree. Each vnode has its own VmoManager based
// on which its FileCache runs by getting and putting Pages. It maps kVmoSize Pages
// to a VmoNode to batch VMO_OP_LOCK and UNLOCK operations. Also, a mapping of
// a VmoNode keeps as long as the VmoNode is alive in |vmo_tree_| to reduce the mapping operation.
class VmoManager {
 public:
  VmoManager() = default;
  VmoManager(const VmoManager &) = delete;
  VmoManager &operator=(const VmoManager &) = delete;
  VmoManager(const VmoManager &&) = delete;
  VmoManager &operator=(const VmoManager &&) = delete;
  ~VmoManager() { Reset(true); }

  zx::result<bool> CreateAndLockVmo(const pgoff_t index) __TA_EXCLUDES(tree_lock_);
  zx_status_t UnlockVmo(const pgoff_t index, const bool evict) __TA_EXCLUDES(tree_lock_);
  zx::result<zx_vaddr_t> GetAddress(pgoff_t index) __TA_EXCLUDES(tree_lock_);
  void Reset(bool shutdown = false) __TA_EXCLUDES(tree_lock_);

 private:
  // It indicates the size of |VmoNode::vmo_| in kPageSize units.
  // Currently, it is set to the f2fs segment size.
  static constexpr size_t kVmoSize = kDefaultBlocksPerSegment;

  pgoff_t GetOffsetInVmoNode(pgoff_t page_index) const;
  pgoff_t GetVmoNodeKey(pgoff_t page_index);

  using VmoTreeTraits = fbl::DefaultKeyedObjectTraits<pgoff_t, VmoNode>;
  using VmoTree = fbl::WAVLTree<pgoff_t, std::unique_ptr<VmoNode>, VmoTreeTraits>;
  zx::result<VmoNode *> FindVmoNodeUnsafe(const pgoff_t index) __TA_REQUIRES_SHARED(tree_lock_);
  zx::result<VmoNode *> GetVmoNodeUnsafe(const pgoff_t index) __TA_REQUIRES(tree_lock_);

  fs::SharedMutex tree_lock_;
  VmoTree vmo_tree_ __TA_GUARDED(tree_lock_);
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_VMO_MANAGER_H_
