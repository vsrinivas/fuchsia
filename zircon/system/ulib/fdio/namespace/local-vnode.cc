// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/string_piece.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

#include "local-vnode.h"

namespace fdio_internal {

fbl::RefPtr<LocalVnode> LocalVnode::Create(fbl::RefPtr<LocalVnode> parent, zx::channel remote,
                                           fbl::String name) {
  auto vn = fbl::AdoptRef(new LocalVnode(std::move(parent), std::move(remote), std::move(name)));
  if (vn->parent_ != nullptr) {
    vn->parent_->children_.push_back(vn);
  }
  return vn;
}

void LocalVnode::Unlink() {
  UnlinkChildren();
  UnlinkFromParent();
}

zx_status_t LocalVnode::SetRemote(zx::channel remote) {
  if (remote_.is_valid()) {
    // Cannot re-bind after initial bind.
    return ZX_ERR_ALREADY_EXISTS;
  }
  if (!children_.is_empty()) {
    // Overlay remotes are disallowed.
    return ZX_ERR_NOT_SUPPORTED;
  }
  remote_ = std::move(remote);
  return ZX_OK;
}

// Returns a child if it has the name |name|.
// Otherwise, returns nullptr.
fbl::RefPtr<LocalVnode> LocalVnode::Lookup(const fbl::StringPiece& name) const {
  auto vn = children_.find_if(
      [&name](const LocalVnode& elem) -> bool { return fbl::StringPiece(elem.Name()) == name; });
  return vn != children_.end() ? vn.CopyPointer() : nullptr;
}

LocalVnode::LocalVnode(fbl::RefPtr<LocalVnode> parent, zx::channel remote, fbl::String name)
    : parent_(std::move(parent)), remote_(std::move(remote)), name_(std::move(name)) {}

void LocalVnode::UnlinkChildren() {
  while (!children_.is_empty()) {
    auto child = children_.pop_front();
    child->UnlinkChildren();
    child->parent_ = nullptr;
  }
}

void LocalVnode::UnlinkFromParent() {
  if (parent_) {
    parent_->children_.erase(*this);
  }
  parent_ = nullptr;
}

zx_status_t EnumerateInternal(const LocalVnode& vn, fbl::StringBuffer<PATH_MAX>* path,
                              const EnumerateCallback& func) {
  size_t original_length = path->length();

  // Add this current node to the path, and enumerate it if it has a remote
  // object.
  path->Append(vn.Name().data(), vn.Name().length());
  if (vn.Remote().is_valid()) {
    func(fbl::StringPiece(path->data(), path->length()), vn.Remote());
  }

  // If we added a non-null path, add a separator and enumerate all the
  // children.
  if (vn.Name().length() > 0) {
    path->Append('/');
  }

  vn.ForAllChildren(
      [&path, &func](const LocalVnode& child) { return EnumerateInternal(child, path, func); });

  // To re-use the same prefix buffer, restore the original buffer length
  // after enumeration has completed.
  path->Resize(original_length);
  return ZX_OK;
}

zx_status_t EnumerateRemotes(const LocalVnode& vn, const EnumerateCallback& func) {
  fbl::StringBuffer<PATH_MAX> path;
  path.Append('/');
  return EnumerateInternal(vn, &path, func);
}

}  // namespace fdio_internal
