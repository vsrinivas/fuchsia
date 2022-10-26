// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

zx_vaddr_t VmoNode::PageIndexToAddress(pgoff_t page_index) {
  return safemath::CheckMul<zx_vaddr_t>(page_index, kPageSize).ValueOrDie();
}

pgoff_t VmoNode::AddressToPageIndex(zx_vaddr_t address) {
  return safemath::CheckDiv<pgoff_t>(address, kPageSize).ValueOrDie();
}

VmoNode::~VmoNode() {
  ZX_DEBUG_ASSERT(!active_pages_);
  if (address_) {
    zx::vmar::root_self()->unmap(address_, PageIndexToAddress(kVmoSize));
  }
  vmo_.reset();
}

zx::result<bool> VmoNode::CreateAndLockVmo(pgoff_t offset) {
  ZX_DEBUG_ASSERT(offset < kVmoSize);
  zx_vaddr_t vmo_size = PageIndexToAddress(kVmoSize);
  if (!vmo_.is_valid()) {
    if (zx_status_t status = zx::vmo::create(vmo_size, ZX_VMO_DISCARDABLE, &vmo_);
        status != ZX_OK) {
      return zx::error(status);
    }
    if (zx_status_t status = zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo_,
                                                        0, vmo_size, &address_);
        status != ZX_OK) {
      vmo_.reset();
      return zx::error(status);
    }
  }

  if (!active_pages_) {
    zx_status_t status = vmo_.op_range(ZX_VMO_OP_TRY_LOCK, 0, vmo_size, nullptr, 0);
    if (status == ZX_ERR_UNAVAILABLE) {
      // When kernel has decommitted any Pages in |vmo_|, the corresponding bits in |page_bitmap_|
      // are cleared too.
      zx_vmo_lock_state_t lock_state;
      status = vmo_.op_range(ZX_VMO_OP_LOCK, 0, vmo_size, &lock_state, sizeof(lock_state));
      uint64_t discarded_offset = AddressToPageIndex(lock_state.discarded_offset);
      uint64_t end_offset =
          safemath::CheckAdd<uint64_t>(lock_state.discarded_offset, lock_state.discarded_size)
              .ValueOrDie();
      end_offset = AddressToPageIndex(fbl::round_up(end_offset, kPageSize));
      for (; discarded_offset < end_offset; ++discarded_offset) {
        page_bitmap_.set(discarded_offset, false);
      }
    }
    ZX_DEBUG_ASSERT(status == ZX_OK);
  }

  bool committed = page_bitmap_[offset];
  if (!committed) {
    page_bitmap_.set(offset, true);
  }
  ++active_pages_;
  return zx::ok(committed);
}

zx_status_t VmoNode::UnlockVmo(pgoff_t offset) {
  ZX_DEBUG_ASSERT(offset < kVmoSize);
  if (--active_pages_) {
    return ZX_OK;
  }
  return vmo_.op_range(ZX_VMO_OP_UNLOCK, 0, PageIndexToAddress(kVmoSize), nullptr, 0);
}

zx::result<zx_vaddr_t> VmoNode::GetAddress(pgoff_t offset) {
  ZX_DEBUG_ASSERT(offset < kVmoSize);
  if (!address_ || !vmo_.is_valid()) {
    return zx::error(ZX_ERR_UNAVAILABLE);
  }
  return zx::ok(safemath::CheckAdd<zx_vaddr_t>(address_, PageIndexToAddress(offset)).ValueOrDie());
}

zx::result<bool> VmoManager::CreateAndLockVmo(const pgoff_t index) __TA_EXCLUDES(tree_lock_) {
  std::lock_guard tree_lock(tree_lock_);
  auto vmo_node_or = GetVmoNodeUnsafe(GetVmoNodeKey(index));
  ZX_DEBUG_ASSERT(vmo_node_or.is_ok());
  return vmo_node_or.value()->CreateAndLockVmo(GetOffsetInVmoNode(index));
}

zx_status_t VmoManager::UnlockVmo(const pgoff_t index, const bool evict) {
  std::lock_guard tree_lock(tree_lock_);
  auto vmo_node_or = FindVmoNodeUnsafe(GetVmoNodeKey(index));
  if (vmo_node_or.is_ok()) {
    if (auto status = vmo_node_or.value()->UnlockVmo(GetOffsetInVmoNode(index)); status != ZX_OK) {
      return status;
    }
    if (evict && !vmo_node_or.value()->GetActivePages()) {
      [[maybe_unused]] auto evicted = vmo_tree_.erase(*vmo_node_or.value());
    }
  }
  return vmo_node_or.status_value();
}

zx::result<zx_vaddr_t> VmoManager::GetAddress(pgoff_t index) {
  fs::SharedLock tree_lock(tree_lock_);
  auto vmo_node_or = FindVmoNodeUnsafe(GetVmoNodeKey(index));
  if (vmo_node_or.is_ok()) {
    return vmo_node_or.value()->GetAddress(GetOffsetInVmoNode(index));
  }
  return zx::error(vmo_node_or.error_value());
}

void VmoManager::Reset(bool shutdown) {
  std::lock_guard tree_lock(tree_lock_);
  pgoff_t prev_key = std::numeric_limits<pgoff_t>::max();
  while (!vmo_tree_.is_empty()) {
    if (shutdown) {
      [[maybe_unused]] auto evicted = vmo_tree_.pop_front();
    } else {
      auto key = (prev_key < std::numeric_limits<pgoff_t>::max()) ? prev_key : 0;
      auto current = vmo_tree_.lower_bound(key);
      if (current == vmo_tree_.end()) {
        break;
      }
      // Unless the |prev_key| Page is evicted, try the next Page.
      if (prev_key == current->GetKey()) {
        ++current;
        if (current == vmo_tree_.end()) {
          break;
        }
      }

      prev_key = current->GetKey();
      if (!current->GetActivePages()) {
        [[maybe_unused]] auto evicted = vmo_tree_.erase(*current);
      }
    }
  }
}

zx::result<VmoNode *> VmoManager::FindVmoNodeUnsafe(const pgoff_t index) {
  if (auto vmo_node = vmo_tree_.find(index); vmo_node != vmo_tree_.end()) {
    return zx::ok(&(*vmo_node));
  }
  return zx::error(ZX_ERR_NOT_FOUND);
}

zx::result<VmoNode *> VmoManager::GetVmoNodeUnsafe(const pgoff_t index) {
  VmoNode *vmo_node = nullptr;
  if (auto vmo_node_or = FindVmoNodeUnsafe(index); vmo_node_or.is_error()) {
    auto new_node = std::make_unique<VmoNode>(index);
    vmo_node = new_node.get();
    vmo_tree_.insert(std::move(new_node));
  } else {
    vmo_node = vmo_node_or.value();
  }
  return zx::ok(vmo_node);
}

pgoff_t VmoManager::GetOffsetInVmoNode(pgoff_t page_index) { return page_index % kVmoSize; }

pgoff_t VmoManager::GetVmoNodeKey(pgoff_t page_index) {
  return page_index - GetOffsetInVmoNode(page_index);
}

}  // namespace f2fs
