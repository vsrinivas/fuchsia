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

// The VFS interface does not declare struct vnode, so
// that implementations may provide their own and avoid
// awkward casting between the base implementation and
// the "subclass" of it.
//
// When using the VFS interface with the common helper
// library and rpc glue, the initial fields of struct
// vnode *must* be VNODE_BASE_FIELDS as defined below
//
// Example:
//
// struct vnode {
//     VNODE_BASE_FIELDS
//     my_fs_t* fs;
//     ...
// };
//
// The ops field is used for dispatch and the refcount
// is used by the generic vn_acquire() and vn_release.
// The flags field is private to the implementation.

#define VNODE_BASE_FIELDS \
    vnode_ops_t* ops; \
    uint32_t flags; \
    uint32_t refcount; \
    mx_handle_t remote;

typedef struct vnode vnode_t;
typedef struct vnode_ops vnode_ops_t;

typedef struct vnattr vnattr_t;
typedef struct vdirent vdirent_t;

typedef struct vdircookie {
    uint64_t n;
    void* p;
} vdircookie_t;

void vn_acquire(vnode_t* vn);
void vn_release(vnode_t* vn);

struct vnode_ops {
    void (*release)(vnode_t* vn);
    // Called when refcount reaches zero.

    mx_status_t (*open)(vnode_t** vn, uint32_t flags);
    // Attempts to open *vn, refcount++ on success.

    mx_status_t (*close)(vnode_t* vn);
    // Closes vn, refcount--

    ssize_t (*read)(vnode_t* vn, void* data, size_t len, size_t off);
    // Read data from vn at offset.

    ssize_t (*write)(vnode_t* vn, const void* data, size_t len, size_t off);
    // Write data to vn at offset.

    mx_status_t (*lookup)(vnode_t* vn, vnode_t** out, const char* name, size_t len);
    // Attempt to find child of vn, child returned with refcount++ on success.
    // Name is len bytes long, and does not include a null terminator.

    mx_status_t (*getattr)(vnode_t* vn, vnattr_t* a);
    // Read attributes of vn.

    mx_status_t (*setattr)(vnode_t* vn, vnattr_t* a);
    // Set attributes of vn.

    mx_status_t (*readdir)(vnode_t* vn, void* cookie, void* dirents, size_t len);
    // Read directory entries of vn, error if not a directory.
    // Cookie must be a buffer of vdircookie_t size or larger.
    // Cookie must be zero'd before first call and will be used by.
    // the readdir implementation to maintain state across calls.
    // To "rewind" and start from the beginning, cookie may be zero'd.

    mx_status_t (*create)(vnode_t* vn, vnode_t** out, const char* name, size_t len, uint32_t mode);
    // Create a new node under vn.
    // Name is len bytes long, and does not include a null terminator.
    // Mode specifies the type of entity to create.

    ssize_t (*ioctl)(vnode_t* vn, uint32_t op, const void* in_buf, size_t in_len, void* out_buf, size_t out_len);
    // Performs the given ioctl op on vn.
    // On success, returns the number of bytes received.

    mx_status_t (*unlink)(vnode_t* vn, const char* name, size_t len, bool must_be_dir);
    // Removes name from directory vn

    mx_status_t (*truncate)(vnode_t* vn, size_t len);
    // Change the size of vn

    mx_status_t (*rename)(vnode_t* olddir, vnode_t* newdir, const char* oldname, size_t oldlen,
                          const char* newname, size_t newlen, bool src_must_be_dir, bool dst_must_be_dir);
    // Renames the path at oldname in olddir to the path at newname in newdir.
    // Unlinks any prior newname if it already exists.

    mx_status_t (*link)(vnode_t* vndir, const char* name, size_t len, vnode_t* target);
    // Creates a hard link to the 'target' vnode with a provided name in vndir

    mx_status_t (*sync)(vnode_t* vn);
    // Syncs the vnode with its underlying storage
};


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

mx_status_t vfs_link(vnode_t* vn, const char* oldpath, const char* newpath,
                     const char** oldpathout, const char** newpathout);
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
