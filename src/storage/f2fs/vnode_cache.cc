// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "f2fs.h"

namespace f2fs {

VnodeCache::VnodeCache() = default;

VnodeCache::~VnodeCache() {
  {
    std::lock_guard list_lock(list_lock_);
    std::lock_guard table_lock(table_lock_);
    ZX_ASSERT(dirty_list_.is_empty());
    ZX_ASSERT(vnode_table_.is_empty());
    ZX_ASSERT(ndirty_ == 0);
    ZX_ASSERT(ndirty_dir_ == 0);
  }
}

void VnodeCache::Reset() {
  {
    std::lock_guard list_lock(list_lock_);
    ZX_ASSERT(dirty_list_.is_empty());
  }

  ForAllVnodes([this](fbl::RefPtr<VnodeF2fs>& vnode) {
    __UNUSED zx_status_t status = Evict(vnode.get());
    return ZX_OK;
  });
}

zx_status_t VnodeCache::ForAllVnodes(Callback callback) {
  fbl::RefPtr<VnodeF2fs> prev_vnode = nullptr;

  while (true) {
    fbl::RefPtr<VnodeF2fs> vnode = nullptr;
    // Scope the lock to prevent letting fbl::RefPtr<VnodeF2fs> destructors from running while
    // it is held.
    {
      std::lock_guard lock(table_lock_);
      if (vnode_table_.is_empty()) {
        return ZX_OK;
      }

      VnodeF2fs* raw_vnode = nullptr;
      if (prev_vnode == nullptr) {
        // Acquire the first node from the front of the cache...
        raw_vnode = &vnode_table_.front();
      } else {
        // ... Acquire all subsequent nodes by iterating from the lower bound of the current node.
        auto current = vnode_table_.lower_bound(prev_vnode->GetKey());
        if (current == vnode_table_.end()) {
          return ZX_OK;
        } else if (current.CopyPointer() != prev_vnode.get()) {
          raw_vnode = current.CopyPointer();
        } else {
          auto next = ++current;
          if (next == vnode_table_.end()) {
            return ZX_OK;
          }
          raw_vnode = next.CopyPointer();
        }
      }

      if (raw_vnode->IsActive()) {
        vnode = fbl::MakeRefPtrUpgradeFromRaw(raw_vnode, table_lock_);
        if (vnode == nullptr) {
          // When it is being recycled, we should wait for deactivation or eviction.
          raw_vnode->WaitForDeactive(table_lock_);
          continue;
        }
      } else {
        // When it is inactive, it is safe to make Refptr.
        vnode = fbl::ImportFromRawPtr(raw_vnode);
        vnode->Activate();
      }
    }
    zx_status_t status = callback(vnode);
    prev_vnode = std::move(vnode);
    if (status == ZX_ERR_STOP) {
      break;
    }
    if (status != ZX_ERR_NEXT && status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t VnodeCache::ForDirtyVnodesIf(Callback cb, Callback cb_if) {
  std::vector<fbl::RefPtr<VnodeF2fs>> dirty_vnodes(0);
  int count = 0;
  {
    std::lock_guard lock(list_lock_);
    dirty_vnodes.resize(ndirty_);
    for (auto iter = dirty_list_.begin(); iter != dirty_list_.end(); iter++) {
      fbl::RefPtr<VnodeF2fs> vn = iter.CopyPointer();
      if (cb_if == nullptr || cb_if(vn) == ZX_OK) {
        dirty_vnodes[count] = std::move(vn);
        count++;
      }
    }
  }

  if (!count) {
    return ZX_OK;
  }

  for (int i = 0; i < count; i++) {
    fbl::RefPtr<VnodeF2fs>& vn = dirty_vnodes[i];
    zx_status_t status = cb(vn);
    if (status == ZX_ERR_STOP) {
      break;
    }
    if (status != ZX_ERR_NEXT && status != ZX_OK) {
      dirty_vnodes.clear();
      dirty_vnodes.shrink_to_fit();
      return status;
    }
  }

  dirty_vnodes.clear();
  dirty_vnodes.shrink_to_fit();
  return ZX_OK;
}

void VnodeCache::Downgrade(VnodeF2fs* raw_vnode) {
  std::lock_guard lock(table_lock_);
  // We resurrect it, so it can be used without strong references in the inactive state
  raw_vnode->ResurrectRef();
  fbl::RefPtr<VnodeF2fs> vnode = fbl::ImportFromRawPtr(raw_vnode);

  // If it has been evicted already, it should be freed.
  if (!(*raw_vnode).fbl::WAVLTreeContainable<VnodeF2fs*>::InContainer()) {
    ZX_ASSERT(!(*raw_vnode).fbl::DoublyLinkedListable<fbl::RefPtr<VnodeF2fs>>::InContainer());
    delete fbl::ExportToRawPtr(&vnode);
    return;
  }

  // TODO: Need to adjust the size of vnode_table_ according to memory pressure

  // It is leaked to keep alive in vnode_table
  __UNUSED auto leak = fbl::ExportToRawPtr(&vnode);
}

zx_status_t VnodeCache::Lookup(const ino_t& ino, fbl::RefPtr<VnodeF2fs>* out) {
  fbl::RefPtr<VnodeF2fs> vnode = nullptr;
  {
    std::lock_guard lock(table_lock_);
    if (zx_status_t status = LookupUnsafe(ino, &vnode); status != ZX_OK) {
      return status;
    }
  }
  ZX_ASSERT(vnode != nullptr);
  *out = std::move(vnode);
  return ZX_OK;
}

zx_status_t VnodeCache::LookupUnsafe(const ino_t& ino, fbl::RefPtr<VnodeF2fs>* out) {
  while (true) {
    auto raw_ptr = vnode_table_.find(ino).CopyPointer();
    if (raw_ptr != nullptr) {
      // When the vnode is active, we should check if it is being recycled.
      if (raw_ptr->IsActive()) {
        *out = fbl::MakeRefPtrUpgradeFromRaw(raw_ptr, table_lock_);
        if (*out == nullptr) {
          // When it is being recycled, we should wait for it to be deactivate.
          raw_ptr->WaitForDeactive(table_lock_);
          continue;
        }
        return ZX_OK;
      }
      // When it is inactive, it is safe to make Refptr.
      *out = fbl::ImportFromRawPtr(raw_ptr);
      (*out)->Activate();
      return ZX_OK;
    }
    break;
  }
  return ZX_ERR_NOT_FOUND;
}

zx_status_t VnodeCache::Evict(VnodeF2fs* vnode) {
  ZX_ASSERT(!(*vnode).fbl::DoublyLinkedListable<fbl::RefPtr<VnodeF2fs>>::InContainer());
  std::lock_guard lock(table_lock_);
  return EvictUnsafe(vnode);
}

zx_status_t VnodeCache::EvictUnsafe(VnodeF2fs* vnode) {
  if (!(*vnode).fbl::WAVLTreeContainable<VnodeF2fs*>::InContainer()) {
    FX_LOGS(INFO) << "EvictUnsafe: " << vnode->GetNameView() << "(" << vnode->GetKey()
                  << ") cannot be found in vnode table";
    return ZX_ERR_NOT_FOUND;
  }
  ZX_ASSERT_MSG(vnode_table_.erase(*vnode) != nullptr, "Cannot find vnode (%u)", vnode->GetKey());
  return ZX_OK;
}

zx_status_t VnodeCache::Add(VnodeF2fs* vnode) {
  {
    std::lock_guard lock(table_lock_);
    if ((*vnode).fbl::WAVLTreeContainable<VnodeF2fs*>::InContainer()) {
      return ZX_ERR_ALREADY_EXISTS;
    }
    vnode_table_.insert(vnode);
  }
  return ZX_OK;
}

zx_status_t VnodeCache::AddDirty(VnodeF2fs* vnode) {
  {
    std::lock_guard lock(list_lock_);
    ZX_ASSERT(vnode != nullptr);
    if ((*vnode).fbl::DoublyLinkedListable<fbl::RefPtr<VnodeF2fs>>::InContainer()) {
      return ZX_ERR_ALREADY_EXISTS;
    } else {
      fbl::RefPtr<VnodeF2fs> dirty_vnode = fbl::MakeRefPtrUpgradeFromRaw(vnode, list_lock_);
      // It should not be nullptr because the element holds its ref_count.
      ZX_ASSERT(dirty_vnode != nullptr);
      dirty_list_.push_back(std::move(dirty_vnode));
      if (vnode->IsDir()) {
        ndirty_dir_++;
      }
      ndirty_++;
    }
  }
  return ZX_OK;
}

zx_status_t VnodeCache::RemoveDirty(VnodeF2fs* vnode) {
  std::lock_guard lock(list_lock_);
  return RemoveDirtyUnsafe(vnode);
}

zx_status_t VnodeCache::RemoveDirtyUnsafe(VnodeF2fs* vnode) {
  ZX_ASSERT(vnode != nullptr);
  if (!(*vnode).fbl::DoublyLinkedListable<fbl::RefPtr<VnodeF2fs>>::InContainer()) {
    return ZX_ERR_NOT_FOUND;
  }
  fbl::RefPtr<VnodeF2fs> clean_vnode = dirty_list_.erase(*vnode);
  if (vnode->IsDir()) {
    ndirty_dir_--;
  }
  ndirty_--;
  return ZX_OK;
}

}  // namespace f2fs
