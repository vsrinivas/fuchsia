// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MEMFS_INCLUDE_LIB_MEMFS_CPP_VNODE_H_
#define SRC_STORAGE_MEMFS_INCLUDE_LIB_MEMFS_CPP_VNODE_H_

// Forwarding file for the new names and locations for the memfs classes.
// TODO(brettw) update users and delete this file.

#include "src/storage/memfs/memfs.h"
#include "src/storage/memfs/vnode.h"
#include "src/storage/memfs/vnode_dir.h"

namespace memfs {

using Vfs = Memfs;
using VnodeMemfs = Vnode;

}  // namespace memfs

#endif  // SRC_STORAGE_MEMFS_INCLUDE_LIB_MEMFS_CPP_VNODE_H_
