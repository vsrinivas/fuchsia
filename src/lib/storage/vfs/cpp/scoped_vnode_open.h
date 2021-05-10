// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_SCOPED_VNODE_OPEN_H_
#define SRC_LIB_STORAGE_VFS_CPP_SCOPED_VNODE_OPEN_H_

#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

namespace fs {

// Some production code and many tests want to perform operations on Vnodes but most operations can
// only occur when the node is "open". Normally the Vfs handles the open and close automatically
// corresponding to fidl connections. But some tests bypass fidl and call methods directly. In
// cases where the these functions require the node to be open, this class can manage opening and
// automatically closing it.
class ScopedVnodeOpen {
 public:
  // This uses an explicit Open() call so errors can be reported.
  ScopedVnodeOpen() = default;
  ~ScopedVnodeOpen() { Close(); }

  zx_status_t Open(Vnode* vn, const VnodeConnectionOptions& opts = VnodeConnectionOptions()) {
    if (vnode_)
      return ZX_ERR_BAD_STATE;
    if (zx_status_t status = vn->OpenValidating(opts, nullptr); status != ZX_OK)
      return status;

    vnode_ = fbl::RefPtr<Vnode>(vn);
    return ZX_OK;
  }

  // Convenience version of Open since most callers will have a RefPtr<DerivedType>.
  template <typename Node>
  zx_status_t Open(const fbl::RefPtr<Node>& node,
                   const VnodeConnectionOptions& opts = VnodeConnectionOptions()) {
    return Open(node.get(), opts);
  }

  // This can be called explicitly if the caller wants to know the status code from the close.
  zx_status_t Close() {
    zx_status_t status = ZX_ERR_BAD_STATE;
    if (vnode_) {
      status = vnode_->Close();
      vnode_.reset();
    }
    return status;
  }

 private:
  fbl::RefPtr<Vnode> vnode_;
};

}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_SCOPED_VNODE_OPEN_H_
