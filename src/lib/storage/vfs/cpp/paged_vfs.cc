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
  // TODO(fxbug.dev/51111) need to detach from PagedVnodes that have back-references to this class.
  // The vnodes are reference counted and can outlive this class.
}

zx::status<> PagedVfs::Init() {
  if (zx_status_t status = zx::pager::create(0, &pager_); status != ZX_OK)
    return zx::error(status);

  return pager_pool_->Init();
}

zx::status<> PagedVfs::SupplyPages(const zx::vmo& node_vmo, uint64_t offset, uint64_t length,
                                   const zx::vmo& aux_vmo, uint64_t aux_offset) {
  return zx::make_status(pager_.supply_pages(node_vmo, offset, length, aux_vmo, aux_offset));
}

zx::status<> PagedVfs::ReportPagerError(const zx::vmo& node_vmo, uint64_t offset, uint64_t length,
                                        zx_status_t err) {
  return zx::make_status(pager_.op_range(ZX_PAGER_OP_FAIL, node_vmo, offset, length, err));
}

zx::status<PagedVfs::VmoCreateInfo> PagedVfs::CreatePagedNodeVmo(fbl::RefPtr<PagedVnode> node,
                                                                 uint64_t size) {
  // Register this node with a unique ID to associated it with pager requests.
  VmoCreateInfo create_info;
  {
    std::lock_guard<std::mutex> lock(vfs_lock_);

    create_info.id = next_node_id_;
    ++next_node_id_;

    paged_nodes_[create_info.id] = std::move(node);
  }

  // Create the VMO itself outside the lock.
  if (auto status =
          pager_.create_vmo(0, pager_pool_->port(), create_info.id, size, &create_info.vmo);
      status != ZX_OK) {
    // On error we need to undo the owning reference from above.
    std::lock_guard<std::mutex> lock(vfs_lock_);
    paged_nodes_.erase(create_info.id);
    return zx::error(status);
  }

  return zx::ok(std::move(create_info));
}

void PagedVfs::UnregisterPagedVmo(uint64_t paged_vmo_id) {
  std::lock_guard<std::mutex> lock(vfs_lock_);

  auto found = paged_nodes_.find(paged_vmo_id);
  if (found == paged_nodes_.end()) {
    ZX_DEBUG_ASSERT(false);  // Should always be found.
    return;                  // Possible race with completion message on another thread, ignore.
  }

  paged_nodes_.erase(found);
}

void PagedVfs::PagerVmoRead(uint64_t node_id, uint64_t offset, uint64_t length) {
  // Hold a reference to the object to ensure it doesn't go out of scope during processing.
  fbl::RefPtr<PagedVnode> node;

  {
    std::lock_guard<std::mutex> lock(vfs_lock_);

    auto found = paged_nodes_.find(node_id);
    if (found == paged_nodes_.end())
      return;  // Possible race with completion message on another thread, ignore.

    node = fbl::RefPtr<PagedVnode>(found->second);
  }

  // Handle the request outside the lock while holding a reference to the node.
  node->VmoRead(offset, length);
}

size_t PagedVfs::GetRegisteredPagedVmoCount() const {
  std::lock_guard<std::mutex> lock(vfs_lock_);
  return paged_nodes_.size();
}

}  // namespace fs
