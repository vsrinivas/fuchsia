// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MEMFS_INCLUDE_LIB_MEMFS_MEMFS_H_
#define LIB_MEMFS_INCLUDE_LIB_MEMFS_MEMFS_H_

#include <lib/async/dispatcher.h>
#include <sync/completion.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

typedef struct memfs_filesystem memfs_filesystem_t;

// Given an async dispatcher, create an in-memory filesystem.
//
// Returns the MemFS filesystem object in |out_fs|. This object
// must be freed by memfs_free_filesystem.
//
// Returns a handle to the root directory in |out_root|.
zx_status_t memfs_create_filesystem(async_dispatcher_t* dispatcher, memfs_filesystem_t** out_fs,
                                    zx_handle_t* out_root);

// Frees a MemFS filesystem, unmounting any sub-filesystems that
// may exist.
//
// Requires that the async handler dispatcher provided to
// |memfs_create_filesystem| still be running.
//
// Signals the optional argument |unmounted| when memfs has torn down.
void memfs_free_filesystem(memfs_filesystem_t* fs, completion_t* unmounted);

// Creates an in-memory filesystem and installs it into the local namespace at
// the given path.
//
// Operations on the filesystem are serviced by the given async dispatcher.
//
// Returns |ZX_ERR_ALREADY_EXISTS| if |path| already exists in the namespace for
// this process.
zx_status_t memfs_install_at(async_dispatcher_t* dispatcher, const char* path);

__END_CDECLS

#endif // LIB_MEMFS_INCLUDE_LIB_MEMFS_MEMFS_H_
