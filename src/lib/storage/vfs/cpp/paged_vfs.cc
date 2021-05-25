// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/paged_vfs.h"

#include <fbl/auto_lock.h>

#include "src/lib/storage/vfs/cpp/paged_vnode.h"

namespace fs {

PagedVfs::PagedVfs(async_dispatcher_t* dispatcher, int num_pager_threads) : ManagedVfs(dispatcher) {
  pager_pool_ = std::make_unique<PagerThreadPool>(*this, num_pager_threads);
}

PagedVfs::~PagedVfs() {
  // We potentially have references to many vnodes in the form of the ones registered as paging
  // handlers. Tell all of these nodes that the VFS is going away outside of the lock.
  //
  // Furthermore, unregistering from this class and the Vfs' live vnode map each requires a lock so
  // releasing them all implicitly will cause a lot of unnecessary locking.
  //
  // This implementation removes the Vfs backpointer in the Vnode and unregisters from the Vfs'
  // live node set from within one lock, avoiding the reentrant unregisteration. Owning references
  // to the nodes are kept during this transition to prevent use-after-free for nodes that may
  // release other nodes as a result of the notification (hopefully won't happen but better to be
  // safe).
  std::map<uint64_t, fbl::RefPtr<PagedVnode>> local_nodes;
  {
    std::lock_guard lock(live_nodes_lock_);

    for (auto& [id, node] : paged_nodes_) {
      local_nodes[id] = fbl::RefPtr<PagedVnode>(node);
      UnregisterVnodeLocked(node);
    }
    paged_nodes_.clear();
  }

  // Notify the nodes of the detach outside the lock. After this loop the vnodes will not call back
  // into this class during destruction.
  for (auto& [id, node] : local_nodes)
    node->WillDestroyVfs();

  // The local_nodes will now release its references which will normally delete the Vnode objects.
}

zx::status<> PagedVfs::Init() {
  if (zx_status_t status = zx::pager::create(0, &pager_); status != ZX_OK)
    return zx::error(status);

  if (auto pool_result = pager_pool_->Init(); pool_result.is_error()) {
    pager_.reset();  // Don't leave half-initialized for the is_initialized() function to work.
    return pool_result;
  }

  return zx::ok();
}

zx::status<> PagedVfs::SupplyPages(const zx::vmo& node_vmo, uint64_t offset, uint64_t length,
                                   const zx::vmo& aux_vmo, uint64_t aux_offset) {
  return zx::make_status(pager_.supply_pages(node_vmo, offset, length, aux_vmo, aux_offset));
}

zx::status<> PagedVfs::ReportPagerError(const zx::vmo& node_vmo, uint64_t offset, uint64_t length,
                                        zx_status_t err) {
  return zx::make_status(pager_.op_range(ZX_PAGER_OP_FAIL, node_vmo, offset, length, err));
}

zx::status<PagedVfs::VmoCreateInfo> PagedVfs::CreatePagedNodeVmo(PagedVnode* node, uint64_t size) {
  // Register this node with a unique ID to associated it with pager requests.
  VmoCreateInfo create_info;
  {
    std::lock_guard lock(live_nodes_lock_);

    create_info.id = next_node_id_;
    ++next_node_id_;

    paged_nodes_[create_info.id] = node;
  }

  // Create the VMO itself outside the lock.
  if (auto status =
          pager_.create_vmo(0, pager_pool_->port(), create_info.id, size, &create_info.vmo);
      status != ZX_OK) {
    // Undo the previous insert.
    std::lock_guard lock(live_nodes_lock_);
    paged_nodes_.erase(create_info.id);
    return zx::error(status);
  }

  return zx::ok(std::move(create_info));
}

void PagedVfs::UnregisterPagedVmo(uint64_t paged_vmo_id) {
  std::lock_guard lock(live_nodes_lock_);

  auto found = paged_nodes_.find(paged_vmo_id);
  if (found == paged_nodes_.end()) {
    ZX_DEBUG_ASSERT(false);  // Should always be found.
    return;
  }

  paged_nodes_.erase(found);
}

void PagedVfs::PagerVmoRead(uint64_t node_id, uint64_t offset, uint64_t length) {
  // Hold a reference to the object to ensure it doesn't go out of scope during processing.
  fbl::RefPtr<PagedVnode> node;

  {
    std::lock_guard lock(live_nodes_lock_);

    auto found = paged_nodes_.find(node_id);
    if (found == paged_nodes_.end())
      return;  // Possible race with completion message on another thread, ignore.

    node = fbl::RefPtr<PagedVnode>(found->second);
  }

  // Handle the request outside the lock while holding a reference to the node.
  node->VmoRead(offset, length);
}

size_t PagedVfs::GetRegisteredPagedVmoCount() const {
  std::lock_guard lock(live_nodes_lock_);
  return paged_nodes_.size();
}

}  // namespace fs
