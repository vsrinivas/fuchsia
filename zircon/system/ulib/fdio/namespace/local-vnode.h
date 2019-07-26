// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits.h>

#include <fbl/function.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/string_piece.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

namespace fdio_internal {

using EnumerateCallback =
    fbl::Function<zx_status_t(const fbl::StringPiece& path, const zx::channel& channel)>;

// Represents a mapping from a string name to a remote connection.
//
// Each LocalVnode may have named children, which themselves may also
// optionally represent remote connections.
//
// This class is thread-compatible.
class LocalVnode : public fbl::RefCounted<LocalVnode>,
                   public fbl::DoublyLinkedListable<fbl::RefPtr<LocalVnode>> {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(LocalVnode);

  // Initializes a new vnode, and attaches a reference to it inside an
  // (optional) parent.
  static fbl::RefPtr<LocalVnode> Create(fbl::RefPtr<LocalVnode> parent, zx::channel remote,
                                        fbl::String name);

  // Recursively unlinks this Vnode's children, and detaches this node from
  // its parent.
  void Unlink();

  // Sets the remote connection of the current vnode.
  // This is only permitted if the current vnode has:
  // - No existing connection, and
  // - No children.
  zx_status_t SetRemote(zx::channel remote);

  // Invoke |Fn()| on all children of this LocalVnode.
  // May be used as a const visitor-pattern for all children.
  //
  // Any status other than ZX_OK returned from |Fn()| will halt iteration
  // immediately and return.
  template <typename Fn>
  zx_status_t ForAllChildren(Fn fn) const {
    for (const LocalVnode& vn : children_) {
      zx_status_t status = fn(vn);
      if (status != ZX_OK) {
        return status;
      }
    }
    return ZX_OK;
  }

  // Returns a child if it has the name |name|.
  // Otherwise, returns nullptr.
  fbl::RefPtr<LocalVnode> Lookup(const fbl::StringPiece& name) const;

  // Remote is "set-once". If it is valid, this class guarantees that
  // the value of |Remote()| will not change for the lifetime of |LocalVnode|.
  const zx::channel& Remote() const { return remote_; }
  const fbl::String& Name() const { return name_; }

 private:
  using ChildList = fbl::DoublyLinkedList<fbl::RefPtr<LocalVnode>>;

  LocalVnode(fbl::RefPtr<LocalVnode> parent, zx::channel remote, fbl::String name);

  void UnlinkChildren();
  void UnlinkFromParent();

  ChildList children_;
  fbl::RefPtr<LocalVnode> parent_;
  zx::channel remote_;
  const fbl::String name_;
};

// Invoke |func| on the (path, channel) pairs for all remotes contained within |vn|.
//
// The path supplied to |func| is the full prefix from |vn|.
zx_status_t EnumerateRemotes(const LocalVnode& vn, const EnumerateCallback& func);

}  // namespace fdio_internal
