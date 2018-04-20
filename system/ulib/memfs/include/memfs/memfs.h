// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/async/dispatcher.h>
#include <sync/completion.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

typedef struct memfs_filesystem memfs_filesystem_t;

__BEGIN_CDECLS

// Given an async dispatcher, create an in-memory filesystem.
//
// Returns the MemFS filesystem object in |vfs_out|. This object
// must be freed by memfs_free_filesystem.
// Returns a handle to the root directory in |root_out|.
zx_status_t memfs_create_filesystem(async_t* async, memfs_filesystem_t** fs_out,
                                    zx_handle_t* root_out);

// Frees a MemFS filesystem, unmounting any sub-filesystems that
// may exist.
//
// Requires that the async handler dispatcher provided to
// |memfs_create_filesystem| still be running.
//
// Signals the optional argument |unmounted| when memfs has torn down.
void memfs_free_filesystem(memfs_filesystem_t* fs, completion_t* unmounted);

// Creates an in-memory file system and installs it into the local namespace at
// the given path.
//
// Operations on the file system are serviced by the given async dispatcher.
//
// Returns |ZX_ERR_ALREADY_EXISTS| if |path| already exists in the namespace for
// this process.
zx_status_t memfs_install_at(async_t* async, const char* path);

__END_CDECLS
