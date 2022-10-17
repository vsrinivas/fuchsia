// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_NAMESPACE_LOCAL_CONNECTION_H_
#define LIB_FDIO_NAMESPACE_LOCAL_CONNECTION_H_

#include <fbl/ref_ptr.h>

#include "sdk/lib/fdio/namespace/local-filesystem.h"
#include "sdk/lib/fdio/namespace/local-vnode.h"

struct fdio;

namespace fdio_internal {

// Create an |fdio_t| with a const view of a local node in the namespace.
//
// This object holds strong references to both the local node and local
// filesystem, which are released on |fdio_t|'s close method.
//
// On failure, nullptr is returned.
zx::result<fbl::RefPtr<fdio>> CreateLocalConnection(fbl::RefPtr<const fdio_namespace> fs,
                                                    fbl::RefPtr<LocalVnode> vn);

// If |io| is a connection to a local Vnode, returns a reference to that LocalVnode.
//
// Otherwise, returns nullptr.
fbl::RefPtr<LocalVnode> GetLocalNodeFromConnectionIfAny(fdio_t* io);

struct DirentIteratorState {
  // The ID of the most recent vnode returned by |LocalVnode::Readdir|.
  uint64_t last_seen = 0;

  // The first directory entry is always ".", but this is emulated
  // and not an actual entry. This boolean tracks if the iterator
  // has returned the "." entry.
  bool encountered_dot = false;
};

}  // namespace fdio_internal

#endif  // LIB_FDIO_NAMESPACE_LOCAL_CONNECTION_H_
