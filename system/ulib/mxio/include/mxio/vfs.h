// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <magenta/listnode.h>
#include <magenta/compiler.h>

#include <stdio.h>
#include <unistd.h>  // ssize_t

__BEGIN_CDECLS

#define VFS_MAX_HANDLES 2

// On Fuchsia, the Block Device is transmitted by file descriptor, rather than
// by path. This can prevent some racy behavior relating to FS start-up.
#ifdef __Fuchsia__
#define FS_FD_BLOCKDEVICE 200
#endif

typedef struct vnattr {
    uint32_t valid;        // mask of which bits to set for setattr
    uint32_t mode;
    uint64_t inode;
    uint64_t size;
    uint64_t nlink;
    uint64_t create_time;  // posix time (seconds since epoch)
    uint64_t modify_time;  // posix time
} vnattr_t;

// mask that identifies what fields to set in setattr
#define ATTR_CTIME  0000001
#define ATTR_MTIME  0000002
#define ATTR_ATIME  0000004  // not yet implemented

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

typedef struct vdirent {
    uint32_t size;
    uint32_t type;
    char name[0];
} vdirent_t;

__END_CDECLS
