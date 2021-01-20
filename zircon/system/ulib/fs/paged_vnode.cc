// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/paged_vfs.h>
#include <fs/paged_vnode.h>

namespace fs {

PagedVnode::PagedVnode(PagedVfs* vfs) : vfs_(vfs) { id_ = vfs_->RegisterNode(this); }

PagedVnode::~PagedVnode() { vfs_->UnregisterNode(id_); }

zx::status<> PagedVnode::EnsureCreateVmo(uint64_t size) {
  if (vmo_)
    return zx::ok();

  auto vfs_or = vfs_->CreatePagedVmo(id_, size);
  if (vfs_or.is_error())
    return vfs_or.take_error();
  vmo_ = std::move(vfs_or).value();
  return zx::ok();
}

}  // namespace fs
