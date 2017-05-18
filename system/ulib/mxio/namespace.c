// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
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

#include "private.h"
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

// refcount is incremented when a mxio_dir references any of its vnodes
// when refcount is nonzero it may not be modified or destroyed
struct mxio_namespace {
    mtx_t lock;
    int32_t refcount;
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

static mxio_t* mxio_dir_create_locked(mxio_ns_t* fs, mxvn_t* vn, mx_handle_t h);

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

static void vn_destroy_children_locked(mxvn_t* parent) {
    mxvn_t* next;
    for (mxvn_t* vn = parent->child; vn; vn = next) {
        next = vn->next;
        if (vn->child) {
            vn_destroy_children_locked(vn);
        }
        if (vn->remote != MX_HANDLE_INVALID) {
            mx_handle_close(vn->remote);
        }
        free(vn);
    }
}

static mx_status_t mxdir_close(mxio_t* io) {
    mxdir_t* dir = (mxdir_t*) io;
    mtx_lock(&dir->ns->lock);
    dir->ns->refcount--;
    mtx_unlock(&dir->ns->lock);
    dir->ns = NULL;
    dir->vn = NULL;
    if (dir->rio.h != MX_HANDLE_INVALID) {
        mx_handle_close(dir->rio.h);
        dir->rio.h = MX_HANDLE_INVALID;
    }
    return NO_ERROR;
}

static mx_status_t mxdir_clone(mxio_t* io, mx_handle_t* handles, uint32_t* types) {
    mxdir_t* dir = (mxdir_t*) io;
    if (dir->rio.h == MX_HANDLE_INVALID) {
        return ERR_NOT_SUPPORTED;
    }
    if ((handles[0] = mxio_service_clone(dir->rio.h)) == MX_HANDLE_INVALID) {
        return ERR_BAD_HANDLE;
    }
    mtx_lock(&dir->ns->lock);
    dir->ns->refcount++;
    mtx_unlock(&dir->ns->lock);
    types[0] = PA_MXIO_REMOTE;
    return 1;
}

mx_status_t mxio_ns_connect(mxio_ns_t* ns, const char* path, mx_handle_t h) {
    mxvn_t* vn = &ns->root;
    mx_status_t r = NO_ERROR;

    mtx_lock(&ns->lock);
    if (path[0] != '/') {
        r = ERR_NOT_FOUND;
        goto done;
    }
    path++;

    // These track the last node we descended
    // through that has a remote fs.  We need
    // this so we can backtrack and open relative
    // to this point.
    mxvn_t* save_vn = NULL;
    const char* save_path = "";

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
                path = "";
            }
        }

        // if there's not a remote filesystem, did we pass one?
        if (vn->remote == MX_HANDLE_INVALID) {
            // if not, we're done
            if (save_vn == NULL) {
                r = ERR_NOT_FOUND;
                break;
            }
            // otherwise roll back
            vn = save_vn;
            path = save_path;
        }

        // hand off to remote filesystem
        r = mxio_service_connect_at(vn->remote, path, h);
        goto done;
    }
    mx_handle_close(h);
done:
    mtx_unlock(&ns->lock);
    return r;
}

// Expects a canonical path (no ..) with no leading
// slash and no trailing slash
static mx_status_t mxdir_open(mxio_t* io, const char* path,
                              int32_t flags, uint32_t mode,
                              mxio_t** out) {
    mxdir_t* dir = (mxdir_t*) io;
    mxvn_t* vn = dir->vn;
    mx_status_t r = NO_ERROR;

    mtx_lock(&dir->ns->lock);
    if ((path[0] == '.') && (path[1] == 0)) {
        goto open_dot;
    }

    // These track the last node we descended
    // through that has a remote fs.  We need
    // this so we can backtrack and open relative
    // to this point.
    mxvn_t* save_vn = NULL;
    const char* save_path = "";

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
    if (r == NO_ERROR) {
        if ((vn->remote == MX_HANDLE_INVALID) && (save_vn != NULL)) {
            // This node doesn't have a remote directory, but it
            // is a child of a node that does, so we try to open
            // relative to that directory for our remote fs
            mx_handle_t h;
            if (mxrio_open_handle_raw(save_vn->remote, save_path, flags, mode, &h) == NO_ERROR) {
                if ((*out = mxio_dir_create_locked(dir->ns, vn, h)) == NULL) {
                    r = ERR_NO_MEMORY;
                } else {
                    r = NO_ERROR;
                }
            }

        } else {
open_dot:
            if ((*out = mxio_dir_create_locked(dir->ns, vn, 0)) == NULL) {
                r = ERR_NO_MEMORY;
            }
        }
    }
    mtx_unlock(&dir->ns->lock);
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
        attr->mode = V_TYPE_DIR | V_IRUSR;
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
    .clone = mxdir_clone,
    .ioctl = mxio_default_ioctl,
    .wait_begin = mxio_default_wait_begin,
    .wait_end = mxio_default_wait_end,
    .unwrap = mxio_default_unwrap,
    .posix_ioctl = mxio_default_posix_ioctl,
    .get_vmo = mxio_default_get_vmo,
};

static mxio_t* mxio_dir_create_locked(mxio_ns_t* ns, mxvn_t* vn, mx_handle_t h) {
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

mx_status_t mxio_ns_destroy(mxio_ns_t* ns) {
    mtx_lock(&ns->lock);
    if (ns->refcount != 0) {
        mtx_unlock(&ns->lock);
        return ERR_BAD_STATE;
    } else {
        vn_destroy_children_locked(&ns->root);
        mtx_unlock(&ns->lock);
        free(ns);
        return NO_ERROR;
    }
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
    if (ns->refcount != 0) {
        r = ERR_BAD_STATE;
        goto done;
    }
    mxvn_t* vn = &ns->root;
    if (path[0] == 0) {
        // the path was "/" so we're trying to bind to the root vnode
        if (vn->remote == MX_HANDLE_INVALID) {
            vn->remote = remote;
        } else {
            r = ERR_ALREADY_EXISTS;
        }
        mtx_unlock(&ns->lock);
        return r;
    }
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
done:
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

mxio_t* mxio_ns_open_root(mxio_ns_t* ns) {
    mtx_lock(&ns->lock);
    mxio_t* io = mxio_dir_create_locked(ns, &ns->root, 0);
    if (io != NULL) {
        ns->refcount++;
    }
    mtx_unlock(&ns->lock);
    return io;
}

int mxio_ns_opendir(mxio_ns_t* ns) {
    mxio_t* io = mxio_ns_open_root(ns);
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
    mxio_t* io = mxio_ns_open_root(ns);
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


static mx_status_t ns_enum_callback(mxvn_t* vn, void* cookie,
                                    mx_status_t (*func)(void* cookie, const char* path,
                                                        size_t len, mx_handle_t h)) {
    char path[PATH_MAX];
    char* end = path + sizeof(path) - 1;
    *end = 0;
    mx_handle_t h = vn->remote;
    for (;;) {
        if ((vn->namelen + 1) > (size_t)(end - path)) {
            return ERR_BAD_PATH;
        }
        end -= vn->namelen;
        memcpy(end, vn->name, vn->namelen);
        if ((vn = vn->parent) == NULL) {
            size_t len = (sizeof(path) - 1) - (end - path);
            if (len > 0) {
                return func(cookie, end, len, h);
            } else {
                // the root vn ends up having length 0, so we
                // fake up a correct canonical name for it here
                return func(cookie, "/", 1, h);
            }
        }
        end--;
        *end = '/';
    }
}

static mx_status_t ns_enumerate(mxvn_t* vn, void* cookie,
                                mx_status_t (*func)(void* cookie, const char* path,
                                                    size_t len, mx_handle_t h)) {
    while (vn != NULL) {
        if (vn->remote != MX_HANDLE_INVALID) {
            ns_enum_callback(vn, cookie, func);
        }
        if (vn->child) {
            ns_enumerate(vn->child, cookie, func);
        }
        vn = vn->next;
    }
    return 0;
}

typedef struct {
    size_t bytes;
    size_t count;
    char* buffer;
    mx_handle_t* handle;
    uint32_t* type;
    char** path;
} export_state_t;

static mx_status_t ns_export_count(void* cookie, const char* path,
                                   size_t len, mx_handle_t h) {
    export_state_t* es = cookie;
    // Each entry needs one slot in the handle table,
    // one slot in the type table, and one slot in the
    // path table, plus storage for the path and NUL
    es->bytes += sizeof(mx_handle_t) + sizeof(uint32_t) + sizeof(char**) + len + 1;
    es->count += 1;
    return NO_ERROR;
}

static mx_status_t ns_export_copy(void* cookie, const char* path,
                                  size_t len, mx_handle_t h) {
    if ((h = mxio_service_clone(h)) == MX_HANDLE_INVALID) {
        return ERR_BAD_STATE;
    }
    export_state_t* es = cookie;
    memcpy(es->buffer, path, len + 1);
    es->path[es->count] = es->buffer;
    es->handle[es->count] = h;
    es->type[es->count] = PA_HND(PA_NS_DIR, es->count);
    es->buffer += (len + 1);
    es->count++;
    return NO_ERROR;
}

mx_status_t mxio_ns_export(mxio_ns_t* ns, mxio_flat_namespace_t** out) {
    export_state_t es;
    es.bytes = sizeof(mxio_flat_namespace_t);
    es.count = 0;

    mtx_lock(&ns->lock);

    ns_enumerate(&ns->root, &es, ns_export_count);

    mxio_flat_namespace_t* flat = malloc(es.bytes);
    if (flat == NULL) {
        mtx_unlock(&ns->lock);
        return ERR_NO_MEMORY;
    }
    // We've allocated enough memory for the flat struct
    // followed by count handles, followed by count types,
    // follewed by count path ptrs followed by enough bytes
    // for all the path strings.  Point es.* at the right
    // slices of that memory:
    es.handle = (mx_handle_t*) (flat + 1);
    es.type = (uint32_t*) (es.handle + es.count);
    es.path = (char**) (es.type + es.count);
    es.buffer = (char*) (es.path + es.count);

    es.count = 0;
    mx_status_t status = ns_enumerate(&ns->root, &es, ns_export_copy);
    mtx_unlock(&ns->lock);

    if (status < 0) {
        for (size_t n = 0; n < es.count; n++) {
            mx_handle_close(es.handle[n]);
        }
        free(flat);
    } else {
        flat->count = es.count;
        flat->handle = es.handle;
        flat->type = es.type;
        flat->path = (const char* const*) es.path;
        *out = flat;
    }

    return status;
}

mx_status_t mxio_ns_export_root(mxio_flat_namespace_t** out) {
    mx_status_t status;
    mtx_lock(&mxio_lock);
    if (mxio_root_ns == NULL) {
        status = ERR_NOT_FOUND;
    } else {
        status = mxio_ns_export(mxio_root_ns, out);
    }
    mtx_unlock(&mxio_lock);
    return status;
}
