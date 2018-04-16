// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <zircon/types.h>
#include <zircon/listnode.h>
#include <zircon/syscalls.h>
#include <zircon/processargs.h>
#include <zircon/device/vfs.h>

#include <fdio/namespace.h>
#include <fdio/remoteio.h>
#include <fdio/util.h>
#include <fdio/vfs.h>

#include "private.h"
#include "private-remoteio.h"


// A fdio namespace is a simple local filesystem that consists
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
// fdio_directory object is created and returned.  This object
// handles further OPEN operations, as well as READDIR and STAT.
// It favors local children over the remote -- so, for example,
// READDIR first returns the vnode's local children, then forwards
// the request to the remote, but filters the results (removing
// matches of its own children).

typedef struct fdio_directory mxdir_t;
typedef struct fdio_vnode mxvn_t;

struct fdio_vnode {
    mxvn_t* child;
    mxvn_t* parent;
    mxvn_t* next;
    zx_handle_t remote;
    uint32_t namelen;
    char name[];
};

// refcount is incremented when a fdio_dir references any of its vnodes
// when refcount is nonzero it may not be modified or destroyed
struct fdio_namespace {
    mtx_t lock;
    int32_t refcount;
    mxvn_t root;
};

// The directory represents a local directory (either / or
// some directory between / and a mount point), so it has
// to emulate directory behavior.
struct fdio_directory {
    // base fdio object
    fdio_t io;

    mxvn_t* vn;
    fdio_ns_t* ns;

    // readdir sequence number
    // TODO: rewind support (when we have rewinddir)
    atomic_int_fast32_t seq;
};

static fdio_t* fdio_dir_create_locked(fdio_ns_t* fs, mxvn_t* vn);

static mxvn_t* vn_lookup_locked(mxvn_t* dir, const char* name, size_t len) {
    for (mxvn_t* vn = dir->child; vn; vn = vn->next) {
        if ((vn->namelen == len) && (!memcmp(vn->name, name, len))) {
            return vn;
        }
    }
    return NULL;
}

static zx_status_t vn_create_locked(mxvn_t* dir, const char* name, size_t len,
                                    zx_handle_t remote, mxvn_t** out) {
    if ((len == 0) || (len > NAME_MAX)) {
        return ZX_ERR_INVALID_ARGS;
    }
    if ((len == 1) && (name[0] == '.')) {
        return ZX_ERR_INVALID_ARGS;
    }
    if ((len == 2) && (name[0] == '.') && (name[1] == '.')) {
        return ZX_ERR_INVALID_ARGS;
    }
    mxvn_t* vn = vn_lookup_locked(dir, name, len);
    if (vn != NULL) {
        // And we do not allow replacing a virtual dir node
        // with a real directory node:
        if (remote != ZX_HANDLE_INVALID) {
            LOG(1, "VN-CREATE FAILED: SHADOWING LOCAL\n");
            return ZX_ERR_ALREADY_EXISTS;
        }
        // if there's already vnode, we do not allow
        // overlapping a remoted vnode:
        if (vn->remote != ZX_HANDLE_INVALID) {
            LOG(1, "VN-CREATE FAILED: SHADOWING REMOTE\n");
            return ZX_ERR_NOT_SUPPORTED;
        }
        *out = vn;
        return ZX_OK;
    }
    if ((vn = calloc(1, sizeof(*vn) + len + 1)) == NULL) {
        return ZX_ERR_NO_MEMORY;
    }
    memcpy(vn->name, name, len);
    vn->name[len] = 0;
    vn->namelen = len;
    vn->parent = dir;
    vn->remote = remote;
    vn->next = dir->child;
    dir->child = vn;
    *out = vn;
    return ZX_OK;
}

// vn_destroy *only* safe to be called on vnodes that have never been
// wrapped in a directory object, because we don't refcount vnodes
// (they're expected to live for the duration of the namespace).
//
// It's used by fdio_ns_bind() to delete intermediate vnodes that
// were created while the ns lock is held, to "undo" a partial mkdir
// operation that failed partway down the pat.  Since the lock is not
// released until the full operation completes, this is safe.
static zx_status_t vn_destroy_locked(mxvn_t* child) {
    // can't destroy a live node
    if (child->remote != ZX_HANDLE_INVALID) {
        return ZX_ERR_BAD_STATE;
    }
    // can't destroy the root
    if (child->parent == NULL) {
        return ZX_ERR_NOT_SUPPORTED;
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
    return ZX_OK;
}

static void vn_destroy_children_locked(mxvn_t* parent) {
    mxvn_t* next;
    for (mxvn_t* vn = parent->child; vn; vn = next) {
        next = vn->next;
        if (vn->child) {
            vn_destroy_children_locked(vn);
        }
        if (vn->remote != ZX_HANDLE_INVALID) {
            zx_handle_close(vn->remote);
        }
        free(vn);
    }
}

static zx_status_t mxdir_close(fdio_t* io) {
    mxdir_t* dir = (mxdir_t*) io;
    mtx_lock(&dir->ns->lock);
    dir->ns->refcount--;
    mtx_unlock(&dir->ns->lock);
    dir->ns = NULL;
    dir->vn = NULL;
    return ZX_OK;
}

static zx_status_t mxdir_clone(fdio_t* io, zx_handle_t* handles, uint32_t* types) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t ns_walk_locked(mxvn_t** _vn, const char** _path) {
    mxvn_t* vn = *_vn;
    const char* path = *_path;

    // Empty path or "." matches initial node.
    if ((path[0] == 0) || ((path[0] == '.') && (path[1] == 0))) {
        return ZX_OK;
    }

    for (;;) {
        // Find the next path segment.
        const char* name = path;
        const char* next = strchr(path, '/');
        size_t len = next ? (size_t)(next - path) : strlen(path);

        // Path segments may not be empty.
        if (len == 0) {
            return ZX_ERR_BAD_PATH;
        }

        // look for a match
        mxvn_t* child = vn_lookup_locked(vn, name, len);
        if (child != NULL) {
            vn = child;
            if (next) {
                // Matched, but more path segments to walk.
                // Descend and continue.
                path = next + 1;
                continue;
            } else {
                // we've matched on the last segment
                *_vn = vn;
                *_path = ".";
                return ZX_OK;
            }
        }

        // If there's remaining path but this is not a mount point,
        // we're done.
        if (vn->remote == ZX_HANDLE_INVALID) {
            return ZX_ERR_NOT_FOUND;
        }

        *_vn = vn;
        *_path = path;
        return ZX_OK;
    }
}

zx_status_t fdio_ns_connect(fdio_ns_t* ns, const char* path,
                            uint32_t flags, zx_handle_t h) {
    mxvn_t* vn = &ns->root;
    zx_status_t r = ZX_OK;

    LOG(6, "CONNECT '%s'\n", path);
    // Require that we start at /
    if (path[0] != '/') {
        r = ZX_ERR_NOT_FOUND;
        goto fail0;
    }
    path++;

    mtx_lock(&ns->lock);

    if ((r = ns_walk_locked(&vn, &path)) != ZX_OK) {
        goto fail1;
    }

    // cannot connect via non-mountpoint nodes
    if (vn->remote == ZX_HANDLE_INVALID) {
        r = ZX_ERR_NOT_SUPPORTED;
        goto fail1;
    }

    r = fdio_open_at(vn->remote, path, flags, h);
    mtx_unlock(&ns->lock);
    return r;

fail1:
    mtx_unlock(&ns->lock);
fail0:
    zx_handle_close(h);
    return r;
}

zx_status_t fdio_ns_open(fdio_ns_t* ns, const char* path, uint32_t flags, zx_handle_t* out) {
    zx_handle_t h;
    if (zx_channel_create(0, &h, out) != ZX_OK) {
        return ZX_ERR_INTERNAL;
    }
    zx_status_t r = fdio_ns_connect(ns, path, flags, h);
    if (r != ZX_OK) {
        zx_handle_close(*out);
        *out = ZX_HANDLE_INVALID;
    }
    return r;
}

// Expects a canonical path (no ..) with no leading
// slash and no trailing slash
static zx_status_t mxdir_open(fdio_t* io, const char* path,
                              uint32_t flags, uint32_t mode,
                              fdio_t** out) {
    mxdir_t* dir = (mxdir_t*) io;
    mxvn_t* vn = dir->vn;
    zx_status_t r = ZX_OK;

    LOG(6, "OPEN '%s'\n", path);
    mtx_lock(&dir->ns->lock);

    if ((r = ns_walk_locked(&vn, &path)) == ZX_OK) {
        if (vn->remote == ZX_HANDLE_INVALID) {
            if ((*out = fdio_dir_create_locked(dir->ns, vn)) == NULL) {
                r = ZX_ERR_NO_MEMORY;
            }
        } else {
            mtx_unlock(&dir->ns->lock);

            // If we're trying to mkdir over top of a mount point,
            // the correct error is EEXIST
            if ((flags & ZX_FS_FLAG_CREATE) && !strcmp(path, ".")) {
                return ZX_ERR_ALREADY_EXISTS;
            }

            // Active Namespaces are immutable, so referencing remote here
            // is safe.  We don't want to do a blocking open under the ns lock.
            r = zxrio_open_handle(vn->remote, path, flags, mode, out);
            LOG(6, "OPEN REMOTE '%s': %d\n", path, r);
            return r;
        }
    }

    mtx_unlock(&dir->ns->lock);
    return r;
}

static zx_status_t fill_dirent(vdirent_t* de, size_t delen,
                               const char* name, size_t len, uint32_t type) {
    size_t sz = sizeof(vdirent_t) + len + 1;

    // round up to uint32 aligned
    if (sz & 3)
        sz = (sz + 3) & (~3);
    if (sz > delen)
        return ZX_ERR_INVALID_ARGS;
    de->size = sz;
    de->type = type;
    memcpy(de->name, name, len);
    de->name[len] = 0;
    return sz;
}

static zx_status_t mxdir_readdir_locked(mxdir_t* dir, void* buf, size_t len) {
    void *ptr = buf;

    zx_status_t r = fill_dirent(ptr, len, ".", 1, VTYPE_TO_DTYPE(V_TYPE_DIR));
    if (r < 0) {
        return 0;
    }
    ptr += r;
    len -= r;

    for (mxvn_t* vn = dir->vn->child; vn; vn = vn->next) {
        if ((r = fill_dirent(ptr, len, vn->name, vn->namelen, VTYPE_TO_DTYPE(V_TYPE_DIR))) < 0) {
            break;
        }
        ptr += r;
        len -= r;
    }

    return ptr - buf;
}

static zx_status_t mxdir_misc(fdio_t* io, uint32_t op, int64_t off,
                              uint32_t maxreply, void* ptr, size_t len) {
    mxdir_t* dir = (mxdir_t*) io;
    zx_status_t r;
    switch (ZXRIO_OP(op)) {
    case ZXRIO_READDIR:
        LOG(6, "READDIR\n");
        mtx_lock(&dir->ns->lock);
        int n = atomic_fetch_add(&dir->seq, 1);
        if (n == 0) {
            r = mxdir_readdir_locked(dir, ptr, maxreply);
        } else {
            r = 0;
        }
        mtx_unlock(&dir->ns->lock);
        return r;
    case ZXRIO_STAT:
        LOG(6, "STAT\n");
        if (maxreply < sizeof(vnattr_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        vnattr_t* attr = ptr;
        memset(attr, 0, sizeof(*attr));
        attr->mode = V_TYPE_DIR | V_IRUSR;
        attr->inode = 1;
        attr->nlink = 1;
        return sizeof(vnattr_t);
    case ZXRIO_UNLINK:
        return ZX_ERR_UNAVAILABLE;
    default:
        LOG(6, "MISC OP %u\n", op);
        return ZX_ERR_NOT_SUPPORTED;
    }
}

ssize_t mxdir_ioctl(fdio_t* io, uint32_t op, const void* in_buf,
                    size_t in_len, void* out_buf, size_t out_len) {
    return ZX_ERR_NOT_SUPPORTED;
}

static fdio_ops_t dir_ops = {
    .read = fdio_default_read,
    .read_at = fdio_default_read_at,
    .write = fdio_default_write,
    .write_at = fdio_default_write_at,
    .recvfrom = fdio_default_recvfrom,
    .sendto = fdio_default_sendto,
    .recvmsg = fdio_default_recvmsg,
    .sendmsg = fdio_default_sendmsg,
    .misc = mxdir_misc,
    .seek = fdio_default_seek,
    .close = mxdir_close,
    .open = mxdir_open,
    .clone = mxdir_clone,
    .ioctl = mxdir_ioctl,
    .wait_begin = fdio_default_wait_begin,
    .wait_end = fdio_default_wait_end,
    .unwrap = fdio_default_unwrap,
    .shutdown = fdio_default_shutdown,
    .posix_ioctl = fdio_default_posix_ioctl,
    .get_vmo = fdio_default_get_vmo,
};

static fdio_t* fdio_dir_create_locked(fdio_ns_t* ns, mxvn_t* vn) {
    mxdir_t* dir = calloc(1, sizeof(*dir));
    if (dir == NULL) {
        return NULL;
    }
    dir->ns = ns;
    dir->vn = vn;
    dir->io.ops = &dir_ops;
    dir->io.magic = FDIO_MAGIC;
    atomic_init(&dir->io.refcount, 1);
    atomic_init(&dir->seq, 0);
    return &dir->io;
}

zx_status_t fdio_ns_create(fdio_ns_t** out) {
    // +1 is for the "" name
    fdio_ns_t* ns = fdio_alloc(sizeof(fdio_ns_t) + 1);
    if (ns == NULL) {
        return ZX_ERR_NO_MEMORY;
    }
    mtx_init(&ns->lock, mtx_plain);
    *out = ns;
    return ZX_OK;
}

zx_status_t fdio_ns_destroy(fdio_ns_t* ns) {
    mtx_lock(&ns->lock);
    if (ns->refcount != 0) {
        mtx_unlock(&ns->lock);
        return ZX_ERR_BAD_STATE;
    } else {
        vn_destroy_children_locked(&ns->root);
        mtx_unlock(&ns->lock);
        free(ns);
        return ZX_OK;
    }
}

zx_status_t fdio_ns_bind(fdio_ns_t* ns, const char* path, zx_handle_t remote) {
    LOG(1, "BIND '%s' %x\n", path, remote);
    if (remote == ZX_HANDLE_INVALID) {
        return ZX_ERR_BAD_HANDLE;
    }
    if ((path == NULL) || (path[0] != '/')) {
        return ZX_ERR_INVALID_ARGS;
    }

    // skip leading slash
    path++;

    zx_status_t r = ZX_OK;

    mtx_lock(&ns->lock);
    mxvn_t* vn = &ns->root;
    if (path[0] == 0) {
        // the path was "/" so we're trying to bind to the root vnode
        if (vn->remote == ZX_HANDLE_INVALID) {
            if (vn->child) {
                // overlay remotes are disallowed
                r = ZX_ERR_NOT_SUPPORTED;
            } else {
                vn->remote = remote;
            }
        } else {
            r = ZX_ERR_ALREADY_EXISTS;
        }
        LOG(1, "BIND ROOT: FAILED\n");
        goto done;
    }
    if (vn->remote != ZX_HANDLE_INVALID) {
        // if there's something mounted at / we can't shadow it
        r = ZX_ERR_NOT_SUPPORTED;
        LOG(1, "BIND: FAILED (root bound)\n");
        goto done;
    }
    for (;;) {
        const char* next = strchr(path, '/');
        if (next) {
            // not the final segment, create an intermediate vnode
            r = vn_create_locked(vn, path, next - path, ZX_HANDLE_INVALID, &vn);
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

zx_status_t fdio_ns_bind_fd(fdio_ns_t* ns, const char* path, int fd) {
    zx_handle_t handle[FDIO_MAX_HANDLES];
    uint32_t type[FDIO_MAX_HANDLES];

    zx_status_t r = fdio_clone_fd(fd, 0, handle, type);
    if (r < 0) {
        return r;
    }
    if (r == 0) {
        return ZX_ERR_INTERNAL;
    }

    if (type[0] != PA_FDIO_REMOTE) {
        // wrong type, discard handles
        for (int n = 0; n < r; n++) {
            zx_handle_close(handle[n]);
        }
        return ZX_ERR_WRONG_TYPE;
    }

    // close any aux handles, then do the actual bind
    for (int n = 1; n < r; n++) {
        zx_handle_close(handle[n]);
    }
    if ((r = fdio_ns_bind(ns, path, handle[0])) < 0) {
        zx_handle_close(handle[0]);
    }
    return r;
}

fdio_t* fdio_ns_open_root(fdio_ns_t* ns) {
    fdio_t* io;
    mtx_lock(&ns->lock);
    if (ns->root.remote == ZX_HANDLE_INVALID) {
        io = fdio_dir_create_locked(ns, &ns->root);
        if (io != NULL) {
            ns->refcount++;
        }
        mtx_unlock(&ns->lock);
    } else {
        mtx_unlock(&ns->lock);
        // Active namespaces are immutable, so safe to access remote
        // outside of the lock, avoiding blocking while holding the lock.
        zx_status_t r = zxrio_open_handle(ns->root.remote, "", O_RDWR, 0, &io);
        if (r != ZX_OK) {
            io = NULL;
        }
    }
    return io;
}

int fdio_ns_opendir(fdio_ns_t* ns) {
    fdio_t* io = fdio_ns_open_root(ns);
    if (io == NULL) {
        errno = ENOMEM;
        return -1;
    }
    int fd = fdio_bind_to_fd(io, -1, 0);
    if (fd < 0) {
        fdio_release(io);
        errno = ENOMEM;
    }
    return fd;
}

zx_status_t fdio_ns_chdir(fdio_ns_t* ns) {
    fdio_t* io = fdio_ns_open_root(ns);
    if (io == NULL) {
        return ZX_ERR_NO_MEMORY;
    }
    fdio_chdir(io, "/");
    return ZX_OK;
}

static zx_status_t ns_enum_callback(mxvn_t* vn, void* cookie,
                                    zx_status_t (*func)(void* cookie, const char* path,
                                                        size_t len, zx_handle_t h)) {
    char path[PATH_MAX];
    char* end = path + sizeof(path) - 1;
    *end = 0;
    zx_handle_t h = vn->remote;
    for (;;) {
        if ((vn->namelen + 1) > (size_t)(end - path)) {
            return ZX_ERR_BAD_PATH;
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

static zx_status_t ns_enumerate(mxvn_t* vn, void* cookie,
                                zx_status_t (*func)(void* cookie, const char* path,
                                                    size_t len, zx_handle_t h)) {
    while (vn != NULL) {
        if (vn->remote != ZX_HANDLE_INVALID) {
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
    zx_handle_t* handle;
    uint32_t* type;
    char** path;
} export_state_t;

static zx_status_t ns_export_count(void* cookie, const char* path,
                                   size_t len, zx_handle_t h) {
    export_state_t* es = cookie;
    // Each entry needs one slot in the handle table,
    // one slot in the type table, and one slot in the
    // path table, plus storage for the path and NUL
    es->bytes += sizeof(zx_handle_t) + sizeof(uint32_t) + sizeof(char**) + len + 1;
    es->count += 1;
    return ZX_OK;
}

static zx_status_t ns_export_copy(void* cookie, const char* path,
                                  size_t len, zx_handle_t h) {
    if ((h = fdio_service_clone(h)) == ZX_HANDLE_INVALID) {
        return ZX_ERR_BAD_STATE;
    }
    export_state_t* es = cookie;
    memcpy(es->buffer, path, len + 1);
    es->path[es->count] = es->buffer;
    es->handle[es->count] = h;
    es->type[es->count] = PA_HND(PA_NS_DIR, es->count);
    es->buffer += (len + 1);
    es->count++;
    return ZX_OK;
}

zx_status_t fdio_ns_export(fdio_ns_t* ns, fdio_flat_namespace_t** out) {
    export_state_t es;
    es.bytes = sizeof(fdio_flat_namespace_t);
    es.count = 0;

    mtx_lock(&ns->lock);

    ns_enumerate(&ns->root, &es, ns_export_count);

    fdio_flat_namespace_t* flat = malloc(es.bytes);
    if (flat == NULL) {
        mtx_unlock(&ns->lock);
        return ZX_ERR_NO_MEMORY;
    }
    // We've allocated enough memory for the flat struct
    // followed by count handles, followed by count types,
    // follewed by count path ptrs followed by enough bytes
    // for all the path strings.  Point es.* at the right
    // slices of that memory:
    es.handle = (zx_handle_t*) (flat + 1);
    es.type = (uint32_t*) (es.handle + es.count);
    es.path = (char**) (es.type + es.count);
    es.buffer = (char*) (es.path + es.count);

    es.count = 0;
    zx_status_t status = ns_enumerate(&ns->root, &es, ns_export_copy);
    mtx_unlock(&ns->lock);

    if (status < 0) {
        for (size_t n = 0; n < es.count; n++) {
            zx_handle_close(es.handle[n]);
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

zx_status_t fdio_ns_export_root(fdio_flat_namespace_t** out) {
    zx_status_t status;
    mtx_lock(&fdio_lock);
    if (fdio_root_ns == NULL) {
        status = ZX_ERR_NOT_FOUND;
    } else {
        status = fdio_ns_export(fdio_root_ns, out);
    }
    mtx_unlock(&fdio_lock);
    return status;
}
