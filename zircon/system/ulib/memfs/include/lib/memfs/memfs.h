// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MEMFS_MEMFS_H_
#define LIB_MEMFS_MEMFS_H_

#include <lib/async/dispatcher.h>
#include <lib/sync/completion.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

typedef struct memfs_filesystem memfs_filesystem_t;

// Given an async dispatcher, create an in-memory filesystem.
//
// The number of pages in this memfs is bounded by the amount of
// available physical memory.
//
// Returns the MemFS filesystem object in |out_fs|. This object
// must be freed by memfs_free_filesystem.
//
// Returns a handle to the root directory in |out_root|.
__EXPORT zx_status_t memfs_create_filesystem(async_dispatcher_t* dispatcher,
                                             memfs_filesystem_t** out_fs, zx_handle_t* out_root);

// Frees a MemFS filesystem, unmounting any sub-filesystems that
// may exist.
//
// Requires that the async handler dispatcher provided to
// |memfs_create_filesystem| still be running.
//
// Signals the optional argument |unmounted| when memfs has torn down.
__EXPORT void memfs_free_filesystem(memfs_filesystem_t* fs, sync_completion_t* unmounted);

// Creates an in-memory filesystem and installs it into the local namespace at
// the given path.
//
// Operations on the filesystem are serviced by the given async dispatcher.
//
// Returns the MemFS filesystem object in |out_fs|.  This object may be freed by
// memfs_uninstall_unsafe. See memfs_uninstall_unsafe for how to avoid use-after-free bugs when
// freeing that memory.
//
// The number of pages in this memfs is bounded by the amount of
// available physical memory.
//
// Returns |ZX_ERR_ALREADY_EXISTS| if |path| already exists in the namespace for
// this process.
__EXPORT zx_status_t memfs_install_at(async_dispatcher_t* dispatcher, const char* path,
                                      memfs_filesystem_t** out_fs);

// Removes the in-memory filesystem |fs| installed into the local namespace at |path|.
//
// If there are pending operations on the file system, uninstalling the file system can result in a
// use-after-free. To avoid this problem, the caller must shutdown the async_dispatcher_t passed to
// memfs_install_at before calling memfs_uninstall_unsafe.
//
// Typically, memfs_uninstall_unsafe is only useful in unit tests where the caller has complete
// control over all pending operations. In production code, prefer to clean up by exiting the
// process.
//
// On error, |fs| is not freed.  Errors may include all errors from fdio_ns_unbind and
// fdio_ns_get_installed.
__EXPORT zx_status_t memfs_uninstall_unsafe(memfs_filesystem_t* fs, const char* path);

__END_CDECLS

#endif  // LIB_MEMFS_MEMFS_H_
