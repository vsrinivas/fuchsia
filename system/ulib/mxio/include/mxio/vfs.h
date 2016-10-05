// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <magenta/listnode.h>
#include <magenta/compiler.h>

// ssize_t?
#include <stdio.h>

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
    uint32_t refcount;

typedef struct vnode vnode_t;
typedef struct vnode_ops vnode_ops_t;

typedef struct vnattr vnattr_t;
typedef struct vdirent vdirent_t;

typedef struct vdircookie {
    uint64_t n;
    void* p;
} vdircookie_t;

#define VFS_MAX_HANDLES 2

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

    mx_status_t (*unlink)(vnode_t* vn, const char* name, size_t len);
    // Removes name from directory vn

    mx_status_t (*truncate)(vnode_t* vn, size_t len);
    // Change the size of vn

    mx_status_t (*rename)(vnode_t* olddir, vnode_t* newdir, const char* oldname, size_t oldlen, const char* newname, size_t newlen);
    // Renames the path at oldname in olddir to the path at newname in newdir.
    // Unlinks any prior newname if it already exists.
};

struct vnattr {
    uint32_t mode;
    uint32_t reserved;
    uint64_t inode;
    uint64_t size;
};

// bits compatible with POSIX stat
#define V_TYPE_MASK 0170000
#define V_TYPE_SOCK 0140000
#define V_TYPE_LINK 0120000
#define V_TYPE_FILE 0100000
#define V_TYPE_BDEV 0060000
#define V_TYPE_DIR  0040000
#define V_TYPE_CDEV 0020000
#define V_TYPE_PIPE 0010000

#define V_ISUID 0004000
#define V_ISGID 0002000
#define V_ISVTX 0001000
#define V_IRWXU 0000700
#define V_IRUSR 0000400
#define V_IWUSR 0000200
#define V_IXUSR 0000100
#define V_IRWXG 0000070
#define V_IRGRP 0000040
#define V_IWGRP 0000020
#define V_IXGRP 0000010
#define V_IRWXO 0000007
#define V_IROTH 0000004
#define V_IWOTH 0000002
#define V_IXOTH 0000001

#define VTYPE_TO_DTYPE(mode) (((mode)&V_TYPE_MASK) >> 12)
#define DTYPE_TO_VTYPE(type) (((type)&15) << 12)

struct vdirent {
    uint32_t size;
    uint32_t type;
    char name[0];
};

void vn_acquire(vnode_t* vn);
void vn_release(vnode_t* vn);

__END_CDECLS
