// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include "vfs.h"

uint32_t __trace_bits;

void vfs_close(vnode_t* vn) {
    trace(VFS, "vfs_close: vn=%p\n", vn);
}

// Starting at vnode vn, walk the tree described by the path string,
// until either there is only one path segment remaining in the string
// or we encounter a vnode that represents a remote filesystem
mx_status_t vfs_walk(vnode_t* vn, vnode_t** out,
                     const char* path, const char** pathout) {
    const char* nextpath;
    mx_status_t r;
    size_t len;

    for (;;) {
        while (path[0] == '/') {
            // discard extra leading /s
            path++;
        }
        if (path[0] == 0) {
            // convert empty initial path of final path segment to "."
            path = ".";
        }
        if ((nextpath = strchr(path, '/')) != NULL) {
            // path has at least one additional segment
            // traverse to the next segment
            len = nextpath - path;
            nextpath++;
            trace(WALK, "vfs_walk: vn=%p name='%.*s' nextpath='%s'\n", vn, (int)len, path, nextpath);
            if ((r = vn->ops->lookup(vn, &vn, path, len))) {
                return r;
            }
            path = nextpath;
        } else {
            // final path segment, we're done here
            trace(WALK, "vfs_walk: vn=%p name='%s' (local)\n", vn, path);
            *out = vn;
            *pathout = path;
            return 0;
        }
    }
    return ERR_NOT_FOUND;
}

mx_status_t vfs_open(vnode_t* vndir, vnode_t** out,
                     const char* path, uint32_t flags, uint32_t mode) {
    trace(VFS, "vfs_open: path='%s' flags=%d\n", path, flags);
    mx_status_t r;
    if ((r = vfs_walk(vndir, &vndir, path, &path)) < 0) {
        return r;
    }

    size_t len = strlen(path);
    vnode_t* vn;
    if (flags & O_CREAT) {
        if ((r = vndir->ops->create(vndir, &vn, path, len, mode)) < 0) {
            if ((r == ERR_ALREADY_EXISTS) && (!(flags & O_EXCL))) {
                goto try_open;
            }
            return r;
        }
    } else {
try_open:
        if ((r = vndir->ops->lookup(vndir, &vn, path, len)) < 0) {
            return r;
        }
        if ((r = vn->ops->open(&vn, flags)) < 0) {
            return r;
        }
    }
    trace(VFS, "vfs_open: vn=%p\n", vn);
    *out = vn;
    return NO_ERROR;
}

mx_status_t vfs_fill_dirent(vdirent_t* de, size_t delen,
                            const char* name, size_t len, uint32_t type) {
    size_t sz = sizeof(vdirent_t) + len + 1;

    // round up to uint32 aligned
    if (sz & 3)
        sz = (sz + 3) & (~3);
    if (sz > delen)
        return ERR_TOO_BIG;
    de->size = sz;
    de->type = type;
    memcpy(de->name, name, len);
    de->name[len] = 0;
    return sz;
}