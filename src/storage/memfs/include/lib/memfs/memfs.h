// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MEMFS_INCLUDE_LIB_MEMFS_MEMFS_H_
#define SRC_STORAGE_MEMFS_INCLUDE_LIB_MEMFS_MEMFS_H_

#include <lib/async/dispatcher.h>
#include <lib/sync/completion.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

typedef struct memfs_filesystem memfs_filesystem_t;

// Create an in-memory filesystem. It will run on the given dispatcher.
//
// The number of pages in this memfs is bounded by the amount of available physical memory.
//
// Returns the MemFS filesystem object in |out_fs|. This object must be freed by
// memfs_free_filesystem().
//
// Returns a handle to the root directory in |out_root|. The caller can install it in the local
// namespace (mount it at a given path for the current process) or use it directly for file
// operations. Many callers will want to install it in the local namespace and can use
// memfs_install_at() for convenience instead of this function.
__EXPORT zx_status_t memfs_create_filesystem(async_dispatcher_t* dispatcher,
                                             memfs_filesystem_t** out_fs, zx_handle_t* out_root);

// Creates an in-memory filesystem and installs it into the local namespace at the given path.
// This is an alternative to memfs_create_filesystem() for convenience; see that function for
// more.
//
// Returns the MemFS filesystem object in |out_fs|. This object must be freed by
// memfs_free_filesystem().
//
// Returns |ZX_ERR_ALREADY_EXISTS| if |path| already exists in the namespace for this process.
__EXPORT zx_status_t memfs_install_at(async_dispatcher_t* dispatcher, const char* path,
                                      memfs_filesystem_t** out_fs);

// Frees a MemFS filesystem, unmounting any sub-filesystems that may exist. If memfs_install_at()
// was used, the installation in the local namespace will be removed before this function returns.
//
// Requires that the async handler dispatcher provided to memfs_create_filesystem() or
// memfs_install_at() still be running. Otherwise the completion argument will never be signaled.
//
// Signals the optional argument |unmounted| when memfs has torn down. If this is null the
// filesystem will still be torn down asynchronously (possibly in the future) which requires that
// the dispatcher still be running. Because the caller can't know when minfs is done cleaning
// up without the completion signal, passing null is only recommended when the dispatcher runs
// for the duration of the process (typically minfs cleanup is not required if the process is
// exiting).
__EXPORT void memfs_free_filesystem(memfs_filesystem_t* fs, sync_completion_t* unmounted);

__END_CDECLS

#endif  // SRC_STORAGE_MEMFS_INCLUDE_LIB_MEMFS_MEMFS_H_
