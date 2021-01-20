// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_PAGED_VFS_H_
#define FS_PAGED_VFS_H_

#include <lib/zx/pager.h>
#include <lib/zx/status.h>
#include <lib/zx/vmo.h>

#include <fs/pager_thread_pool.h>
#include <fs/vfs.h>

namespace fs {

class PagedVnode;

// A variant of Vfs that supports paging. A PagedVfs supports PagedVnode objects.
//
// UNDER DEVELOPMENT
// =================
// Paging in the fs library is currently under active development and not ready to use yet.
// See bug 51111.
class PagedVfs : public Vfs {
 public:
  // The caller must call Init() which must succeed before using this class.
  explicit PagedVfs(int num_pager_threads);
  ~PagedVfs() override;

  // Creates the pager and worker threads. If any of these fail, this class should no be used.
  zx::status<> Init();

  // Called in response to a successful PagedVnode::VmoRead() request, this supplies paged data from
  // aux_vmo to the PagedVnode's VMO to the kernel. See zx_pager_supply_pages() documentation for
  // more.
  zx::status<> SupplyPages(PagedVnode& node, uint64_t offset, uint64_t length, zx::vmo& aux_vmo,
                           uint64_t aux_offset);

  // Called in response to a failed PagedVnode::VmoRead() request, this reports that there was an
  // error populating page data. See zx_pager_op_range() documentation for more.
  zx::status<> ReportPagerError(PagedVnode& node, uint32_t op, uint64_t offset, uint64_t length,
                                uint64_t data);

  // Tracks all PagedVnodes. Each Vnode is assigned a unique ID and tracked in a map in this class
  // for associating page requests from the kernel to nodes. PagedNodes should register on
  // construction and unregister on destruction.
  uint64_t RegisterNode(PagedVnode* node);
  void UnregisterNode(uint64_t id);

  // Allocates a VMO of the given size, associated with the given PagedVnode ID. VMOs for use with
  // the pager must be allocated by this method so the page requests are routed to the correct
  // PagedVnode.
  //
  // This function is for itnernal use by PagedVnode. Most callers should use
  // PagedVnode::EnsureCreateVmo().
  zx::status<zx::vmo> CreatePagedVmo(uint64_t node_id, uint64_t size);

  // Callbacks that the PagerThreadPool uses to notify us of pager events. These calls will get
  // issued on arbitrary threads.
  void PagerVmoRead(uint64_t node_id, uint64_t offset, uint64_t length);
  void PagerVmoComplete(uint64_t node_id);

 private:
  PagerThreadPool pager_pool_;  // Threadsafe, does not need locking.
  zx::pager pager_;

  uint64_t next_node_id_ = 1;
  std::map<uint64_t, PagedVnode*> paged_nodes_ FS_TA_GUARDED(vfs_lock_);
};

}  // namespace fs

#endif  // FS_PAGED_VFS_H_
