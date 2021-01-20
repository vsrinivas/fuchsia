// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_PAGED_VNODE_H_
#define FS_PAGED_VNODE_H_

#include <fs/vnode.h>

namespace fs {

class PagedVfs;

// A Vnode that supports paged I/O.
//
// To implement, derive from this class and:
//  - Implement Vnode::GetVmo().
//     - Use PagedVnode::EnsureCreateVmo() to create the data mapping. This will create it in such a
//       way that it's registered with the paging system for callbacks.
//     - Do vmo().create_child() to clone the VMO backing this node.
//     - Set the rights on the cloned VMO with the rights passed to GetVmo().
//     - Populate the GetVmo() out parameter with the child VMO.
//  - Implement VmoRead() to fill the VMO data when requested.
class PagedVnode : public Vnode {
 public:
  ~PagedVnode() override;

  // TODO(fxbug.dev/51111) References to this class can outlife the PagedVfs which can render this
  // pointer invalid. See TODO in ~PagedVfs. Need to revisit lifetimes.
  PagedVfs* vfs() { return vfs_; }
  zx::vmo& vmo() { return vmo_; }

  // Called by the paging system in response to a kernel request to fill data into this node's VMO.
  //
  //  - On success, calls vfs()->SupplyPages() with the created data range.
  //  - On failure, calls vfs()->ReportPagerError() with the error information.
  //
  // The success or failure cases can happen synchronously (from within this call stack) or
  // asynchronously in the future. Failure to report success or failure will hang the requesting
  // process.
  //
  // Note that offset + length will be page-aligned so can extend beyond the end of the file.
  virtual void VmoRead(uint64_t offset, uint64_t length) = 0;

 protected:
  explicit PagedVnode(PagedVfs* vfs);

  // Populates the vmo() if necessary. Does nothing if it already exists. Access the created vmo
  // with this class' vmo() getter.
  zx::status<> EnsureCreateVmo(uint64_t size);

 private:
  PagedVfs* vfs_;  // Non-owning.
  uint64_t id_;    // Unique ID assigned by the PagedVfs.

  zx::vmo vmo_;
};

}  // namespace fs

#endif  // FS_PAGED_VNODE_H_
