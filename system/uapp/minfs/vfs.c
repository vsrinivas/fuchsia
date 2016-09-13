// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vfs.h"
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

struct vnode {
    VNODE_BASE_FIELDS
};

uint32_t __trace_bits;

mx_status_t vfs_close(vnode_t* vn) {
    trace(VFS, "vfs_close: vn=%p\n", vn);
    mx_status_t r = vn->ops->close(vn);
    vn_release(vn);
    return r;
}

// Starting at vnode vn, walk the tree described by the path string,
// until either there is only one path segment remaining in the string
// or we encounter a vnode that represents a remote filesystem
mx_status_t vfs_walk(vnode_t* vn, vnode_t** out,
                     const char* path, const char** pathout) {
    vnode_t* oldvn = NULL;
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
            r = vn->ops->lookup(vn, &vn, path, len);
            if (oldvn) {
                // release the old vnode, even if there was an error
                vn_release(oldvn);
            }
            if (r) {
                return r;
            }
            oldvn = vn;
            path = nextpath;
        } else {
            // final path segment, we're done here
            trace(WALK, "vfs_walk: vn=%p name='%s' (local)\n", vn, path);
            if (oldvn == NULL) {
                // returning our original vnode, need to upref it
                vn_acquire(vn);
            }
            *out = vn;
            *pathout = path;
            return 0;
        }
    }
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
            vn_release(vndir);
            return r;
        } else {
            vn_release(vndir);
        }
    } else {
    try_open:
        r = vndir->ops->lookup(vndir, &vn, path, len);
        vn_release(vndir);
        if (r < 0) {
            return r;
        }
        if ((r = vn->ops->open(&vn, flags)) < 0) {
            vn_release(vn);
            return r;
        }
    }
    trace(VFS, "vfs_open: vn=%p\n", vn);
    *out = vn;
    return NO_ERROR;
}

mx_status_t vfs_rename(vnode_t* vndir, const char* oldpath, const char* newpath) {
    vnode_t* oldparent, *newparent;
    mx_status_t r;
    if ((r = vfs_walk(vndir, &oldparent, oldpath, &oldpath)) < 0) {
        return r;
    } else if ((r = vfs_walk(vndir, &newparent, newpath, &newpath)) < 0) {
        vn_release(oldparent);
        return r;
    }
    r = vndir->ops->rename(oldparent, newparent, oldpath, strlen(oldpath), newpath, strlen(newpath));
    vn_release(oldparent);
    vn_release(newparent);
    return r;
}

mx_status_t vfs_fill_dirent(vdirent_t* de, size_t delen,
                            const char* name, size_t len, uint32_t type) {
    size_t sz = sizeof(vdirent_t) + len + 1;

    // round up to uint32 aligned
    if (sz & 3)
        sz = (sz + 3) & (~3);
    if (sz > delen)
        return ERR_INVALID_ARGS;
    de->size = sz;
    de->type = type;
    memcpy(de->name, name, len);
    de->name[len] = 0;
    return sz;
}

void vn_acquire(vnode_t* vn) {
    trace(REFS, "acquire vn=%p ref=%u\n", vn, vn->refcount);
    vn->refcount++;
}

void vn_release(vnode_t* vn) {
    trace(REFS, "release vn=%p ref=%u\n", vn, vn->refcount);
    if (vn->refcount == 0) {
        panic("vn %p: ref underflow\n", vn);
    }
    vn->refcount--;
    if (vn->refcount == 0) {
        trace(VFS, "vfs_release: vn=%p\n", vn);
        vn->ops->release(vn);
    }
}
