// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "trace.h"

#include <mxio/debug.h>
#include <mxio/remoteio.h>

#include <stdlib.h>
#include <stdint.h>
#ifdef __Fuchsia__
#include <threads.h>
#endif
#include <sys/types.h>

#include <magenta/compiler.h>
#include <magenta/types.h>

#include <mxio/vfs.h>
#include <mxio/dispatcher.h>

#define MXDEBUG 0

// VFS Helpers (vfs.c)
#define V_FLAG_DEVICE 1
#define V_FLAG_VMOFILE 2
#define V_FLAG_MOUNT_READY 4

// On Fuchsia, the Block Device is transmitted by file descriptor, rather than
// by path. This can prevent some racy behavior relating to FS start-up.
#ifdef __Fuchsia__
#define FS_FD_BLOCKDEVICE 200
#endif

__BEGIN_CDECLS

// A lock which should be used to protect lookup and walk operations
#ifdef __Fuchsia__
extern mtx_t vfs_lock;
#endif
extern mxio_dispatcher_t* vfs_dispatcher;

// The following functions must be defined by the filesystem linking
// with this VFS layer.

// Extract handle(s), type, and extra info from a vnode.
//  - type == '0' means the vn represents a non-local device.
//  - If the vnode can be acquired, it is acquired by this function.
//  - Returns the number of handles acquired.
mx_status_t vfs_get_handles(vnode_t* vn, uint32_t flags, mx_handle_t* hnds,
                            uint32_t* type, void* extra, uint32_t* esize);
// Handle incoming mxrio messages.
mx_status_t vfs_handler(mxrio_msg_t* msg, mx_handle_t rh, void* cookie);
// Called when a request is made to watch a directory.
ssize_t vfs_do_ioctl_watch_dir(vnode_t* vn,
                               const void* in_buf, size_t in_len,
                               void* out_buf, size_t out_len);
// Called when something is added to a watched directory.
void vfs_notify_add(vnode_t* vn, const char* name, size_t len);
#ifdef __Fuchsia__
// Called to implement filesystem-specific ioctls
ssize_t vfs_do_local_ioctl(vnode_t* vn, uint32_t op, const void* in_buf,
                           size_t in_len, void* out_buf, size_t out_len);
#endif

typedef struct vfs_iostate {
    vnode_t* vn;
    vdircookie_t dircookie;
    size_t io_off;
    uint32_t io_flags;
} vfs_iostate_t;

mx_status_t vfs_open(vnode_t* vndir, vnode_t** out, const char* path,
                     const char** pathout, uint32_t flags, uint32_t mode);

mx_status_t vfs_walk(vnode_t* vn, vnode_t** out,
                     const char* path, const char** pathout);

mx_status_t vfs_unlink(vnode_t* vn, const char* path, size_t len);

mx_status_t vfs_rename(vnode_t* vn, const char* oldpath, const char* newpath,
                       const char** oldpathout, const char** newpathout);

mx_status_t vfs_close(vnode_t* vn);

ssize_t vfs_do_ioctl(vnode_t* vn, uint32_t op, const void* in_buf, size_t in_len,
                     void* out_buf, size_t out_len);

mx_status_t vfs_fill_dirent(vdirent_t* de, size_t delen,
                            const char* name, size_t len, uint32_t type);

// Pins a handle to a remote filesystem onto a vnode, if possible.
mx_status_t vfs_install_remote(vnode_t* vn, mx_handle_t h);
// Unpin a handle to a remote filesystem from a vnode, if one exists.
mx_status_t vfs_uninstall_remote(vnode_t* vn, mx_handle_t* h);

// Send an unmount signal on a handle to a filesystem and await a response.
mx_status_t vfs_unmount_handle(mx_handle_t h, mx_time_t timeout);

// Unpins all remote filesystems in the current filesystem, and waits for the
// response of each one with the provided timeout.
mx_status_t vfs_uninstall_all(mx_time_t timeout);

// Acquire a handle to the vnode. vn_acquires vn if successful.
mx_status_t vfs_create_handle(vnode_t* vn, uint32_t flags, mx_handle_t* out);
// Generic implementation of vfs_handler, which dispatches messages to fs operations.
mx_status_t vfs_handler_generic(mxrio_msg_t* msg, mx_handle_t rh, void* cookie);

__END_CDECLS
