// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_VFS_TYPES_H_
#define SRC_STORAGE_BLOBFS_VFS_TYPES_H_

// This file exists to provide a central place to manage the Vfs types based on the status of the
// in-progress paging backend replacement.
//
// TODO(http://fxbug.dev/51111) convert to using the new interface everywhere and delete this file.

#if defined(ENABLE_BLOBFS_NEW_PAGER)
#include <fs/paged_vfs.h>
#include <fs/paged_vnode.h>
#else
#include <fs/managed_vfs.h>
#include <fs/vnode.h>
#endif

namespace blobfs {

#if defined(ENABLE_BLOBFS_NEW_PAGER)
using VfsType = fs::PagedVfs;
using VnodeType = fs::PagedVnode;
#else
using VfsType = fs::ManagedVfs;
using VnodeType = fs::Vnode;
#endif

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_VFS_TYPES_H_
