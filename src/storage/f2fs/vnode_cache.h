// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_VNODE_CACHE_H_
#define SRC_STORAGE_F2FS_VNODE_CACHE_H_

namespace f2fs {

class VnodeCache {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(VnodeCache);
  VnodeCache();
  ~VnodeCache();

  // It checks if there is vnode for |ino| in vnode_table_, and
  // it returns ZX_OK with valid |out| if it find it.
  // Otherwise, it returns ZX_ERR_NOT_FOUND.
  // When a caller tries to look up a vnode that is being recyclyed,
  // it will be blocked until it gets inactive (deactivated) and
  // valid ref_count.
  zx_status_t Lookup(const ino_t& ino, fbl::RefPtr<VnodeF2fs>* out);
  zx_status_t LookupUnsafe(const ino_t& ino, fbl::RefPtr<VnodeF2fs>* out)
      __TA_REQUIRES(table_lock_);

  // It tries to evict |vnode| from vnode_table_.
  // It returns ZX_ERR_NOT_FOUND if it cannot find |vnode| in the table.
  // A caller should ensure that |vnode| does not exist in dirty_list_.
  zx_status_t Evict(VnodeF2fs* vnode);
  zx_status_t EvictUnsafe(VnodeF2fs* vnode) __TA_REQUIRES(table_lock_);

  // It tries to add |vnode| to vnode_table_.
  // It returns ZX_ERR_ALREADY_EXISTS if it is already in the table.
  zx_status_t Add(VnodeF2fs* vnode);

  // It tries to add/remove |vnode| to/from dirty_list_.
  zx_status_t AddDirty(VnodeF2fs* vnode);
  zx_status_t RemoveDirty(VnodeF2fs* vnode);
  zx_status_t RemoveDirtyUnsafe(VnodeF2fs* vnode) __TA_REQUIRES(list_lock_);
  void Downgrade(VnodeF2fs* raw_vnode);

  // It erases every element in vnode_table_. A caller should ensure that
  // dirty_list_ is empty.
  void Reset();

  using Callback = fbl::Function<zx_status_t(fbl::RefPtr<VnodeF2fs>&)>;

  // It traverses dirty_lists and executes cb for the dirty vnodes with
  // which cb_if returns ZX_OK.
  zx_status_t ForDirtyVnodesIf(Callback cb, Callback cb_if = nullptr);

  // It traverses vnode_tables and execute cb with every vnode.
  zx_status_t ForAllVnodes(Callback callback);

  bool IsDirtyListEmpty() __TA_EXCLUDES(list_lock_) {
    bool ret = false;
    fs::SharedLock lock(list_lock_);
    ret = dirty_list_.is_empty();
    ZX_ASSERT(ret == (ndirty_ == 0));
    return ret;
  }

 private:
  // All vnode pointers including dirty vnodes are insered in vnode_table_, and
  // f2fs tries to evict invalid vnodes (nlink_ = 0) at every checkpoint or VnodeF2fs::Recycle().
  // To make inactive (ref = 0) vnodes keep alive in vnode_table_,
  // it resurrects valid vnode pointers at VnodeF2fs::Recycle().
  // TODO: Eviction policy needs to consider memory pressure.
  using VnodeTableTraits = fbl::DefaultKeyedObjectTraits<ino_t, VnodeF2fs>;
  using VnodeTable = fbl::WAVLTree<ino_t, VnodeF2fs*, VnodeTableTraits>;

  // It is intented that dirty_list_ keeps valid dirty vnodes as keeping their ref_count.
  // Once dirty vnode are inserted in dirty_list_, it never happen that their ref_count
  // reach 0 and VnodeF2fs::Recycle is called before their eviction.
  // Every checkpoint f2fs traverses dirty_list_ to write out dirty vnodes and to purge invalid
  // dirty vnodes. A valid dirty vnode continues to be kept in vnode_tables_ regardless of eviction
  // while invalid dirty nodes are deleted in Vnode::Recycle().
  // Must not acquire kNodeTrunc lock when evciting invalid dirty nodes.
  using DirtyVnodeList = fbl::DoublyLinkedList<fbl::RefPtr<VnodeF2fs>>;

  fs::SharedMutex table_lock_{};
  fs::SharedMutex list_lock_{};
  VnodeTable vnode_table_ __TA_GUARDED(table_lock_){};
  DirtyVnodeList dirty_list_ __TA_GUARDED(list_lock_){};
  uint64_t ndirty_dir_ __TA_GUARDED(list_lock_){0};
  uint64_t ndirty_ __TA_GUARDED(list_lock_){0};
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_VNODE_CACHE_H_
