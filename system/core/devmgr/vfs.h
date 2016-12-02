// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "trace.h"

#include <stdint.h>
#include <magenta/types.h>
#include <mxio/vfs.h>

// VFS Helpers (vfs.c)
#define V_FLAG_DEVICE 1
#define V_FLAG_REMOTE 2
#define V_FLAG_VMOFILE 4

mx_status_t vfs_open(vnode_t* vndir, vnode_t** out, const char* path,
                     const char** pathout, uint32_t flags, uint32_t mode);

mx_status_t vfs_walk(vnode_t* vn, vnode_t** out,
                     const char* path, const char** pathout);

mx_status_t vfs_rename(vnode_t* vn, const char* oldpath, const char* newpath,
                       mx_handle_t rh);

mx_status_t vfs_close(vnode_t* vn);

mx_status_t vfs_fill_dirent(vdirent_t* de, size_t delen,
                            const char* name, size_t len, uint32_t type);
