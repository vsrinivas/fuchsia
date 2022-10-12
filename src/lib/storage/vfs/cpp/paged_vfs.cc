// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/paged_vfs.h"

#include <zircon/syscalls-next.h>

#include <mutex>

#include <fbl/auto_lock.h>

#include "src/lib/storage/vfs/cpp/paged_vnode.h"

namespace fs {

PagedVfs::PagedVfs(async_dispatcher_t* dispatcher, int num_pager_threads) : ManagedVfs(dispatcher) {
  pager_pool_ = std::make_unique<PagerThreadPool>(*this, num_pager_threads);
}

PagedVfs::~PagedVfs() {
  // The pager pool runs threads that get references to nodes and then makes callouts to them.  At
  // this point, however, anything derived from PagedVfs will be in a partially destructed state,
  // which means those callouts are potentially dangerous.  For this reason, the pager pool *must*
  // have been destroyed before this runs.
  ZX_ASSERT(!pager_pool_ || !pager_pool_->IsRunning());

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
  std::vector<fbl::RefPtr<PagedVnode>> local_nodes;
  {
    std::lock_guard lock(live_nodes_lock_);

    for (auto& [id, raw] : paged_nodes_) {
      auto node = fbl::MakeRefPtrUpgradeFromRaw(raw, live_nodes_lock_);
      if (node) {
        local_nodes.push_back(std::move(node));
      }
    }
    paged_nodes_.clear();
  }

  // Notify the nodes of the detach outside the lock. After this loop the vnodes will not call back
  // into this class during destruction.
  for (auto& node : local_nodes)
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

void PagedVfs::TearDown() {
  // See the assertion at the top of ~PagedVfs.
  pager_pool_.reset();

  // After tearing down the pager pool, there's no more opportunity to receive on-no-children events
  // so we should forcibly tear down the nodes to prevent reference cycles (which will manifest as
  // leaks).
  std::vector<fbl::RefPtr<PagedVnode>> nodes;
  {
    std::lock_guard lock(live_nodes_lock_);
    for (const auto& [id, raw] : paged_nodes_) {
      auto node = fbl::MakeRefPtrUpgradeFromRaw(raw, live_nodes_lock_);
      if (node)
        nodes.push_back(std::move(node));
    }
  }
  for (auto& node : nodes) {
    node->TearDown();
  }
}

std::vector<zx::unowned_thread> PagedVfs::GetPagerThreads() const {
  return pager_pool_->GetPagerThreads();
}

zx::status<> PagedVfs::SupplyPages(const zx::vmo& node_vmo, uint64_t offset, uint64_t length,
                                   const zx::vmo& aux_vmo, uint64_t aux_offset) {
  return zx::make_status(pager_.supply_pages(node_vmo, offset, length, aux_vmo, aux_offset));
}

zx::status<> PagedVfs::DirtyPages(const zx::vmo& node_vmo, uint64_t offset, uint64_t length) {
  return zx::make_status(pager_.op_range(ZX_PAGER_OP_DIRTY, node_vmo, offset, length, 0));
}

zx::status<> PagedVfs::ReportPagerError(const zx::vmo& node_vmo, uint64_t offset, uint64_t length,
                                        zx_status_t err) {
  return zx::make_status(pager_.op_range(ZX_PAGER_OP_FAIL, node_vmo, offset, length, err));
}

zx::status<PagedVfs::VmoCreateInfo> PagedVfs::CreatePagedNodeVmo(PagedVnode* node, uint64_t size,
                                                                 uint32_t options) {
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
          pager_.create_vmo(options, pager_pool_->port(), create_info.id, size, &create_info.vmo);
      status != ZX_OK) {
    // Undo the previous insert.
    std::lock_guard lock(live_nodes_lock_);
    paged_nodes_.erase(create_info.id);
    return zx::error(status);
  }

  return zx::ok(std::move(create_info));
}

void PagedVfs::FreePagedVmo(VmoCreateInfo info) {
  // The system calls to detach the pager and free the VMO can be done outside the lock. There is
  // a race where the VMO is destroyed but still in the map and a previously-pending read comes into
  // PagerVmoRead(). But this is unavoidable because the Vnode::VmoRead call happens outside the
  // live nodes lock.
  pager_.detach_vmo(info.vmo);
  info.vmo = zx::vmo();

  std::lock_guard lock(live_nodes_lock_);

  auto found = paged_nodes_.find(info.id);
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
    if (found == paged_nodes_.end()) {
      // When we detach a paged VMO from the pager, there could still be pager requests that
      // we've already dequeued but haven't proceeded yet. These requests will be internally
      // canceled by the kernel. We can't use the COMPLETE message from the kernel because there
      // can be multiple pager threads which may process requests out-of-order. So just ignore stale
      // reads (there's nothing else we can do anyway).
      return;
    }

    node = fbl::MakeRefPtrUpgradeFromRaw(found->second, live_nodes_lock_);
    if (!node)
      return;
  }

  // Handle the request outside the lock while holding a reference to the node.
  node->VmoRead(offset, length);
}

void PagedVfs::PagerVmoDirty(uint64_t node_id, uint64_t offset, uint64_t length) {
  // Hold a reference to the object to ensure it doesn't go out of scope during processing.
  fbl::RefPtr<PagedVnode> node;

  {
    std::lock_guard lock(live_nodes_lock_);

    auto found = paged_nodes_.find(node_id);
    if (found == paged_nodes_.end()) {
      // Ignore stale dirty request as in VmoRead().
      return;
    }

    node = fbl::MakeRefPtrUpgradeFromRaw(found->second, live_nodes_lock_);
    if (!node)
      return;
  }

  // Handle the request outside the lock while holding a reference to the node.
  node->VmoDirty(offset, length);
}

size_t PagedVfs::GetRegisteredPagedVmoCount() const {
  std::lock_guard lock(live_nodes_lock_);
  return paged_nodes_.size();
}

}  // namespace fs
