// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <magenta/types.h>
#include <magenta/listnode.h>
#include <magenta/syscalls.h>
#include <magenta/processargs.h>

#include <mxio/namespace.h>
#include <mxio/remoteio.h>
#include <mxio/util.h>
#include <mxio/vfs.h>

#include "private-remoteio.h"

// A mxio namespace is a simple local filesystem that consists
// of a tree of vnodes, each of which may contain child vnodes
// and a handle for a remote filesystem.
//
// They are expected to be relatively small (perhaps 10-50 total
// local vnodes, acting as roots for the remote filesystems that
// contain the actual items of interest) and as such have a simple
// locking model -- one namespace-wide lock that is held while
// doing the local directory walk part of an OPEN operation.
//
// If an OPEN path matches one of the local vnodes exactly, a
// mxio_directory object is created and returned.  This object
// handles further OPEN operations, as well as READDIR and STAT.
// It favors local children over the remote -- so, for example,
// READDIR first returns the vnode's local children, then forwards
// the request to the remote, but filters the results (removing
// matches of its own children).

typedef struct mxio_directory mxdir_t;
typedef struct mxio_vnode mxvn_t;

struct mxio_vnode {
    mxvn_t* child;
    mxvn_t* parent;
    mxvn_t* next;
    mx_handle_t remote;
    uint32_t namelen;
    char name[];
};

struct mxio_namespace {
    mtx_t lock;
    mxvn_t root;
};

// The directory "subclasses" rio so that it can
// call remoteio ops it needs to pass through to
// the underlying remote fs.
struct mxio_directory {
    mxrio_t rio;
    mxvn_t* vn;
    mxio_ns_t* ns;

    // readdir sequence number
    // TODO: rewind support (when we have rewinddir)
    atomic_int_fast32_t seq;
};

static mxio_t* mxio_dir_create(mxio_ns_t* fs, mxvn_t* vn, mx_handle_t h);

static mxvn_t* vn_lookup_locked(mxvn_t* dir, const char* name, size_t len) {
    for (mxvn_t* vn = dir->child; vn; vn = vn->next) {
        if ((vn->namelen == len) && (!memcmp(vn->name, name, len))) {
            return vn;
        }
    }
    return NULL;
}

static mx_status_t vn_create_locked(mxvn_t* dir, const char* name, size_t len,
                                    mx_handle_t remote, mxvn_t** out) {
    if ((len == 0) || (len > NAME_MAX)) {
        return ERR_INVALID_ARGS;
    }
    if ((len == 1) && (name[0] == '.')) {
        return ERR_INVALID_ARGS;
    }
    if ((len == 2) && (name[0] == '.') && (name[1] == '.')) {
        return ERR_INVALID_ARGS;
    }
    mxvn_t* vn = vn_lookup_locked(dir, name, len);
    if (vn != NULL) {
        // if there's already a vnode, that's okay as long
        // as we don't want to override its remote
        if (remote != MX_HANDLE_INVALID) {
            return ERR_ALREADY_EXISTS;
        }
        *out = vn;
        return NO_ERROR;
    }
    if ((vn = calloc(1, sizeof(*vn) + len + 1)) == NULL) {
        return ERR_NO_MEMORY;
    }
    memcpy(vn->name, name, len);
    vn->name[len] = 0;
    vn->namelen = len;
    vn->parent = dir;
    vn->remote = remote;
    vn->next = dir->child;
    dir->child = vn;
    *out = vn;
    return NO_ERROR;
}

// vn_destroy *only* safe to be called on vnodes that have never been
// wrapped in a directory object, because we don't refcount vnodes
// (they're expected to live for the duration of the namespace).
//
// It's used by mxio_ns_bind() to delete intermediate vnodes that
// were created while the ns lock is held, to "undo" a partial mkdir
// operation that failed partway down the pat.  Since the lock is not
// released until the full operation completes, this is safe.
static mx_status_t vn_destroy_locked(mxvn_t* child) {
    // can't destroy a live node
    if (child->remote != MX_HANDLE_INVALID) {
        return ERR_BAD_STATE;
    }
    // can't destroy the root
    if (child->parent == NULL) {
        return ERR_NOT_SUPPORTED;
    }
    mxvn_t* dir = child->parent;

    if (dir->child == child) {
        dir->child = child->next;
    } else {
        for (mxvn_t* vn = dir->child; vn; vn = vn->next) {
            if (vn->next == child) {
                vn->next = child->next;
                break;
            }
        }
    }
    free(child);
    return NO_ERROR;
}

static mx_status_t mxdir_close(mxio_t* io) {
    mxdir_t* dir = (mxdir_t*) io;
    dir->ns = NULL;
    dir->vn = NULL;
    if (dir->rio.h != MX_HANDLE_INVALID) {
        mx_handle_close(dir->rio.h);
        dir->rio.h = MX_HANDLE_INVALID;
    }
    return NO_ERROR;
}

// Expects a canonical path (no ..) with no leading
// slash and no trailing slash
static mx_status_t mxdir_open(mxio_t* io, const char* path,
                              int32_t flags, uint32_t mode,
                              mxio_t** out) {
    mxdir_t* dir = (mxdir_t*) io;
    mxvn_t* vn = dir->vn;
    mx_status_t r;

    if ((path[0] == '.') && (path[1] == 0)) {
        goto open_dot;
    }

    // These track the last node we descended
    // through that has a remote fs.  We need
    // this so we can backtrack and open relative
    // to this point.
    mxvn_t* save_vn = NULL;
    const char* save_path = "";

    mtx_lock(&dir->ns->lock);
    for (;;) {
        // find the next path segment
        const char* name = path;
        const char* next = strchr(path, '/');
        size_t len = next ? (size_t)(next - path) : strlen(path);

        // path segment can't be empty
        if (len == 0) {
            r = ERR_BAD_PATH;
            break;
        }

        // is there a local match?
        mxvn_t* child = vn_lookup_locked(vn, name, len);
        if (child != NULL) {
            vn = child;
            if (next) {
                // match, but more path to walk
                // descend and continue
                path = next + 1;
                // but remember this node and path
                if (child->remote != MX_HANDLE_INVALID) {
                    save_vn = child;
                    save_path = path;
                }
                continue;
            } else {
                // match on the last segment
                // this is it
                r = NO_ERROR;
                break;
            }
        }

        // if there's not a remote filesystem, give up
        if (vn->remote == MX_HANDLE_INVALID) {
            r = ERR_NOT_FOUND;
            break;
        }

        // hand off to remote filesystem
        mtx_unlock(&dir->ns->lock);
        return mxrio_open_handle(vn->remote, path, flags, mode, out);
    }
    mtx_unlock(&dir->ns->lock);
    if (r == NO_ERROR) {
        if ((vn->remote == MX_HANDLE_INVALID) && (save_vn != NULL)) {
            // This node doesn't have a remote directory, but it
            // is a child of a node that does, so we try to open
            // relative to that directory for our remote fs
            mx_handle_t h;
            if (mxrio_open_handle_raw(save_vn->remote, save_path, flags, mode, &h) == NO_ERROR) {
                if ((*out = mxio_dir_create(dir->ns, vn, h)) == NULL) {
                    return ERR_NO_MEMORY;
                }
                return NO_ERROR;
            }

        }
open_dot:
        if ((*out = mxio_dir_create(dir->ns, vn, 0)) == NULL) {
            return ERR_NO_MEMORY;
        }
    }
    return r;
}

static mx_status_t fill_dirent(vdirent_t* de, size_t delen,
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

static mx_status_t mxdir_readdir_locked(mxdir_t* dir, void* buf, size_t len) {
    void *ptr = buf;

    for (mxvn_t* vn = dir->vn->child; vn; vn = vn->next) {
        mx_status_t r = fill_dirent(ptr, len, vn->name, vn->namelen,
                                    VTYPE_TO_DTYPE(V_TYPE_DIR));
        if (r < 0) {
            break;
        }
        ptr += r;
        len -= r;
    }

    return ptr - buf;
}

// walk a set of directory entries and filter out any that match
// our children, by setting their names to ""
static mx_status_t mxdir_filter(mxdir_t* dir, void* ptr, size_t len) {
    size_t r = len;
    mtx_lock(&dir->ns->lock);
    for (;;) {
        if (len < sizeof(vdirent_t)) {
            break;
        }
        vdirent_t* vde = ptr;
        if (len < vde->size) {
            break;
        }
        ptr += vde->size;
        len -= vde->size;
        size_t namelen = strlen(vde->name);
        for (mxvn_t* vn = dir->vn->child; vn; vn = vn->next) {
            if ((namelen == vn->namelen) &&
                !strcmp(vn->name, vde->name)) {
                vde->name[0] = 0;
                break;
            }
        }
    }
    mtx_unlock(&dir->ns->lock);
    return r;
}


static mx_status_t mxdir_misc(mxio_t* io, uint32_t op, int64_t off,
                              uint32_t maxreply, void* ptr, size_t len) {
    mxdir_t* dir = (mxdir_t*) io;
    mx_status_t r;
    switch (MXRIO_OP(op)) {
    case MXRIO_READDIR:
        //TODO(swetland): Make more robust in the face of large
        // numbers of local children, callers with tiny buffers (none
        // such exist as yet, it's a private interface to mxio), and
        // ideally integrate with a local ordering cookie w/ readdir().
        mtx_lock(&dir->ns->lock);
        for (;;) {
            int n = atomic_fetch_add(&dir->seq, 1);
            if (n > 0) {
                mtx_unlock(&dir->ns->lock);
                if (dir->rio.h == MX_HANDLE_INVALID) {
                    return 0;
                }
                if ((r = mxrio_misc(io, op, off, maxreply, ptr, len)) < 0) {
                    return r;
                }
                return mxdir_filter(dir, ptr, r);
            }
            if ((r = mxdir_readdir_locked(dir, ptr, maxreply)) != 0) {
                // if this returns 0, there are no local children,
                // so we continue the loop where we'll try to readdir
                // the remote fs
                break;
            }
        }
        mtx_unlock(&dir->ns->lock);
        return r;
    case MXRIO_STAT:
        if (maxreply < sizeof(vnattr_t)) {
            return ERR_INVALID_ARGS;
        }
        vnattr_t* attr = ptr;
        memset(attr, 0, sizeof(*attr));
        attr->mode = 0555;
        attr->inode = 1;
        attr->nlink = 1;
        return sizeof(vnattr_t);
    default:
        return ERR_NOT_SUPPORTED;
    }
}

static mxio_ops_t dir_ops = {
    .read = mxio_default_read,
    //.read_at = mxio_default_read_at,
    .write = mxio_default_write,
    //.write_at = mxio_default_write_at,
    .recvmsg = mxio_default_recvmsg,
    .sendmsg = mxio_default_sendmsg,
    .misc = mxdir_misc,
    .seek = mxio_default_seek,
    .close = mxdir_close,
    .open = mxdir_open,
    .clone = mxio_default_clone,
    .ioctl = mxio_default_ioctl,
    .wait_begin = mxio_default_wait_begin,
    .wait_end = mxio_default_wait_end,
    .unwrap = mxio_default_unwrap,
    .posix_ioctl = mxio_default_posix_ioctl,
    .get_vmo = mxio_default_get_vmo,
};

static mxio_t* mxio_dir_create(mxio_ns_t* ns, mxvn_t* vn, mx_handle_t h) {
    mxdir_t* dir = calloc(1, sizeof(*dir));
    if (dir == NULL) {
        if (h != MX_HANDLE_INVALID) {
            mx_handle_close(h);
        }
        return NULL;
    }
    dir->rio.io.ops = &dir_ops;
    dir->rio.io.magic = MXIO_MAGIC;
    atomic_init(&dir->rio.io.refcount, 1);
    if (h != MX_HANDLE_INVALID) {
        dir->rio.h = h;
    } else {
        dir->rio.h = mxio_service_clone(vn->remote);
    }
    dir->ns = ns;
    dir->vn = vn;
    atomic_init(&dir->seq, 0);
    return &dir->rio.io;
}

mx_status_t mxio_ns_create(mxio_ns_t** out) {
    // +1 is for the "" name
    mxio_ns_t* ns = calloc(1, sizeof(mxio_ns_t) + 1);
    if (ns == NULL) {
        return ERR_NO_MEMORY;
    }
    mtx_init(&ns->lock, mtx_plain);
    *out = ns;
    return NO_ERROR;
}

mx_status_t mxio_ns_bind(mxio_ns_t* ns, const char* path, mx_handle_t remote) {
    if (remote == MX_HANDLE_INVALID) {
        return ERR_BAD_HANDLE;
    }
    if ((path == NULL) || (path[0] != '/')) {
        return ERR_INVALID_ARGS;
    }

    // skip leading slash
    path++;

    mx_status_t r = NO_ERROR;

    mtx_lock(&ns->lock);
    mxvn_t* vn = &ns->root;
    for (;;) {
        const char* next = strchr(path, '/');
        if (next) {
            // not the final segment, create an intermediate vnode
            r = vn_create_locked(vn, path, next - path, MX_HANDLE_INVALID, &vn);
            if (r < 0) {
                break;
            }
            path = next + 1;
        } else {
            // final segment. create leaf vnode and stop
            r = vn_create_locked(vn, path, strlen(path), remote, &vn);
            break;
        }
    }

    if (r < 0) {
        // we failed, so unwind, removing any intermediate vnodes
        // we created.  vn_destroy_locked() will error out on any
        // vnode that has a remote, or on the root vnode, so we
        // it will stop us before we remove anything that already
        // existed.  (we never create leaf vnodes with no remote)
        for (;;) {
            mxvn_t* parent = vn->parent;
            if (vn_destroy_locked(vn) < 0) {
                break;
            }
            vn = parent;
        }
    }
    mtx_unlock(&ns->lock);
    return r;
}

mx_status_t mxio_ns_bind_fd(mxio_ns_t* ns, const char* path, int fd) {
    mx_handle_t handle[MXIO_MAX_HANDLES];
    uint32_t type[MXIO_MAX_HANDLES];

    mx_status_t r = mxio_clone_fd(fd, 0, handle, type);
    if (r < 0) {
        return r;
    }
    if (r == 0) {
        return ERR_INTERNAL;
    }

    if (type[0] != PA_MXIO_REMOTE) {
        // wrong type, discard handles
        for (int n = 0; n < r; n++) {
            mx_handle_close(handle[n]);
        }
        return ERR_WRONG_TYPE;
    }

    // close any aux handles, then do the actual bind
    for (int n = 1; n < r; n++) {
        mx_handle_close(handle[n]);
    }
    if ((r = mxio_ns_bind(ns, path, handle[0])) < 0) {
        mx_handle_close(handle[0]);
    }
    return r;
}

int mxio_ns_opendir(mxio_ns_t* ns) {
    mxio_t* io = mxio_dir_create(ns, &ns->root, 0);
    if (io == NULL) {
        errno = ENOMEM;
        return -1;
    }
    int fd = mxio_bind_to_fd(io, -1, 0);
    if (fd < 0) {
        mxio_release(io);
        errno = ENOMEM;
    }
    return fd;
}

mx_status_t mxio_ns_chdir(mxio_ns_t* ns) {
    mxio_t* io = mxio_dir_create(ns, &ns->root, 0);
    if (io == NULL) {
        return ERR_NO_MEMORY;
    }
    mxio_chdir(io, "/");
    return NO_ERROR;
}

mx_status_t mxio_ns_install(mxio_ns_t* ns) {
    //TODO
    return ERR_NOT_SUPPORTED;
}