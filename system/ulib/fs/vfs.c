// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef __Fuchsia__
#include <magenta/syscalls.h>
#endif

#include <mxio/dispatcher.h>
#include <mxio/remoteio.h>

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vfs-internal.h"

uint32_t __trace_bits;

// Trim a name before sending it to internal filesystem functions.
// Trailing '/' characters imply that the name must refer to a directory.
static mx_status_t vfs_name_trim(const char* name, size_t len, size_t* len_out, bool* dir_out) {
    bool is_dir = false;
    while ((len > 0) && name[len - 1] == '/') {
        len--;
        is_dir = true;
    }

    // 'name' should not contain paths consisting of exclusively '/' characters.
    if (len == 0) {
        return ERR_INVALID_ARGS;
    } else if (len > NAME_MAX) {
        return ERR_BAD_PATH;
    }

    *len_out = len;
    *dir_out = is_dir;
    return NO_ERROR;
}

// Access the remote handle if it's ready -- otherwise, return an error.
static mx_status_t vfs_get_remote(vnode_t* vn) {
#ifdef __Fuchsia__
    if (!(vn->flags & V_FLAG_MOUNT_READY)) {
        mx_status_t status = mx_handle_wait_one(vn->remote,
                                                MX_USER_SIGNAL_0 | MX_CHANNEL_PEER_CLOSED, 0,
                                                NULL);
        if (status != NO_ERROR) {
            // Not set (or otherwise remote is bad)
            return ERR_UNAVAILABLE;
        }
        vn->flags |= V_FLAG_MOUNT_READY;
    }
    return vn->remote;
#else
    return ERR_NOT_SUPPORTED;
#endif
}

static mx_status_t vfs_walk_remote(vnode_t* vn, vnode_t** out, const char* path,
                                   const char** pathout, vnode_t* oldvn) {
    trace(WALK, "vfs_walk: vn=%p name='%s' (remote)\n", vn, path);
    mx_status_t r = vfs_get_remote(vn);
    if (r < 0) {
        return r;
    }
    *out = vn;
    *pathout = path;
    if (oldvn == NULL) {
        // returning our original vnode, need to upref it
        vn_acquire(vn);
    }
    return r;
}

static mx_status_t vfs_walk_next(vnode_t* vn, vnode_t** out, const char* path,
                                 const char* nextpath, const char** pathout, vnode_t** oldvn) {
    // path has at least one additional segment
    // traverse to the next segment
    size_t len = nextpath - path;
    nextpath++;
    trace(WALK, "vfs_walk: vn=%p name='%.*s' nextpath='%s'\n", vn, (int)len, path, nextpath);
    mx_status_t r = vn->ops->lookup(vn, out, path, len);
    assert(r <= 0);
    if (*oldvn) {
        // release the old vnode, even if there was an error
        vn_release(*oldvn);
    }
    if (r < 0) {
        return r;
    }
    *oldvn = *out;
    *pathout = nextpath;
    return NO_ERROR;
}

static void vfs_walk_final(vnode_t* vn, vnode_t** out, const char* path, const char** pathout,
                           vnode_t* oldvn) {
    // final path segment, we're done here
    trace(WALK, "vfs_walk: vn=%p name='%s' (local)\n", vn, path);
    if (oldvn == NULL) {
        // returning our original vnode, need to upref it
        vn_acquire(vn);
    }
    *out = vn;
    *pathout = path;
}

// Starting at vnode vn, walk the tree described by the path string,
// until either there is only one path segment remaining in the string
// or we encounter a vnode that represents a remote filesystem
//
// If a non-negative status is returned, the vnode at 'out' has been acquired.
// Otherwise, no net deltas in acquires/releases occur.
mx_status_t vfs_walk(vnode_t* vn, vnode_t** out,
                     const char* path, const char** pathout) {
    vnode_t* oldvn = NULL;
    mx_status_t r;

    for (;;) {
        while (path[0] == '/') {
            // discard extra leading /s
            path++;
        }
        if (path[0] == 0) {
            // convert empty initial path of final path segment to "."
            path = ".";
        }
        if ((vn->remote > 0) && (!(vn->flags & V_FLAG_DEVICE))) {
            // remote filesystem mount, caller must resolve
            // devices are different, so ignore them even though they can have vn->remote
            return vfs_walk_remote(vn, out, path, pathout, oldvn);
        }

        char* nextpath = strchr(path, '/');
        bool additional_segment = false;
        if (nextpath != NULL) {
            char* end = nextpath;
            while (*end != '\0') {
                if (*end != '/') {
                    additional_segment = true;
                    break;
                }
                end++;
            }
        }
        if (additional_segment) {
            if ((r = vfs_walk_next(vn, &vn, path, nextpath, &path, &oldvn)) != NO_ERROR) {
                return r;
            }
        } else {
            vfs_walk_final(vn, out, path, pathout, oldvn);
            return NO_ERROR;
        }
    }
}

mx_status_t vfs_open(vnode_t* vndir, vnode_t** out, const char* path,
                     const char** pathout, uint32_t flags, uint32_t mode) {
    trace(VFS, "vfs_open: path='%s' flags=%d\n", path, flags);
    mx_status_t r;
    if ((r = vfs_walk(vndir, &vndir, path, &path)) < 0) {
        return r;
    }
    if (r > 0) {
        // remote filesystem, return handle and path through to caller
        vn_release(vndir);
        *pathout = path;
        return r;
    }

    if ((flags & O_CREAT) && (flags & O_NOREMOTE)) {
        return ERR_INVALID_ARGS;
    }

    size_t len = strlen(path);
    vnode_t* vn;

    bool must_be_dir = false;
    if ((r = vfs_name_trim(path, len, &len, &must_be_dir)) != NO_ERROR) {
        return r;
    }

    if (flags & O_CREAT) {
        if (must_be_dir && !S_ISDIR(mode)) {
            return ERR_INVALID_ARGS;
        }
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
        if (flags & O_NOREMOTE) {
            // Opening a mount point: Do NOT traverse across remote.
            if (!(vn->remote > 0)) {
                // There must be a remote handle mounted on this vnode.
                vn_release(vn);
                return ERR_BAD_STATE;
            }
        } else if ((vn->remote > 0) && (!(vn->flags & V_FLAG_DEVICE))) {
            // Opening a mount point: Traverse across remote.
            // Devices are different, even though they also have remotes.  Ignore them.
            *pathout = ".";
            r = vfs_get_remote(vn);
            vn_release(vn);
            return r;
        }

#ifdef __Fuchsia__
        flags |= (must_be_dir ? O_DIRECTORY : 0);
#endif
        r = vn->ops->open(&vn, flags);
        // Open and lookup both incremented the refcount. Release it once for
        // opening a vnode.
        vn_release(vn);
        if (r < 0) {
            return r;
        }
        if (flags & O_TRUNC) {
            if ((r = vn->ops->truncate(vn, 0)) < 0) {
                vn_release(vn);
                return r;
            }
        }
    }
    trace(VFS, "vfs_open: vn=%p\n", vn);
    *pathout = "";
    *out = vn;
    return NO_ERROR;
}

mx_status_t vfs_unlink(vnode_t* vndir, const char* path, size_t len) {
    bool must_be_dir;
    mx_status_t r;
    if ((r = vfs_name_trim(path, len, &len, &must_be_dir)) != NO_ERROR) {
        return r;
    }
    return vndir->ops->unlink(vndir, path, len, must_be_dir);
}

mx_status_t vfs_rename(vnode_t* vndir, const char* oldpath, const char* newpath,
                       const char** oldpathout, const char** newpathout) {
    vnode_t* oldparent, *newparent;
    mx_status_t r = 0, r_old, r_new;

    if ((r_old = vfs_walk(vndir, &oldparent, oldpath, &oldpath)) < 0) {
        return r_old;
    }
    if ((r_new = vfs_walk(vndir, &newparent, newpath, &newpath)) < 0) {
        vn_release(oldparent);
        return r_new;
    }

    if (r_old != r_new) {
        // Rename can only be directed to one filesystem
        r = ERR_NOT_SUPPORTED;
        goto done;
    }

    if (r_old == 0) {
        // Local filesystem
        size_t oldlen = strlen(oldpath);
        size_t newlen = strlen(newpath);
        bool old_must_be_dir;
        bool new_must_be_dir;
        if ((r = vfs_name_trim(oldpath, oldlen, &oldlen, &old_must_be_dir)) != NO_ERROR) {
            goto done;
        }
        if ((r = vfs_name_trim(newpath, newlen, &newlen, &new_must_be_dir)) != NO_ERROR) {
            goto done;
        }
        r = vndir->ops->rename(oldparent, newparent, oldpath, oldlen, newpath, newlen,
                               old_must_be_dir, new_must_be_dir);
    } else {
        // Remote filesystem -- forward the request
        *oldpathout = oldpath;
        *newpathout = newpath;
        r = r_old;
    }

done:
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

ssize_t vfs_do_ioctl(vnode_t* vn, uint32_t op, const void* in_buf,
                     size_t in_len, void* out_buf, size_t out_len) {
    switch (op) {
#ifdef __Fuchsia__
    case IOCTL_DEVICE_WATCH_DIR: {
        return vfs_do_ioctl_watch_dir(vn, in_buf, in_len, out_buf, out_len);
    }
    case IOCTL_DEVMGR_MOUNT_FS: {
        if ((in_len != 0) || (out_len != sizeof(mx_handle_t))) {
            return ERR_INVALID_ARGS;
        }
        mx_handle_t h0, h1;
        mx_status_t status;
        if ((status = mx_channel_create(0, &h0, &h1)) < 0) {
            return status;
        }
        if ((status = vfs_install_remote(vn, h1)) < 0) {
            mx_handle_close(h0);
            mx_handle_close(h1);
            return status;
        }
        memcpy(out_buf, &h0, sizeof(mx_handle_t));
        return sizeof(mx_handle_t);
    }
    case IOCTL_DEVMGR_UNMOUNT_NODE: {
        return vfs_uninstall_remote(vn);
    }
    case IOCTL_DEVMGR_UNMOUNT_FS: {
        vfs_uninstall_all();
        vn->ops->ioctl(vn, op, in_buf, in_len, out_buf, out_len);
        exit(0);
    }
#endif
    default:
#ifdef __Fuchsia__
        return vfs_do_local_ioctl(vn, op, in_buf, in_len, out_buf, out_len);
#else
        return vn->ops->ioctl(vn, op, in_buf, in_len, out_buf, out_len);
#endif
    }
}

void vn_acquire(vnode_t* vn) {
    trace(REFS, "acquire vn=%p ref=%u\n", vn, vn->refcount);
    vn->refcount++;
}

// TODO(orr): figure out x-system panic
#define panic(fmt...) do { fprintf(stderr, fmt); __builtin_trap(); } while (0)

void vn_release(vnode_t* vn) {
    trace(REFS, "release vn=%p ref=%u\n", vn, vn->refcount);
    if (vn->refcount == 0) {
        panic("vn %p: ref underflow\n", vn);
    }
    vn->refcount--;
    if (vn->refcount == 0) {
        assert(!(vn->remote > 0));
        trace(VFS, "vfs_release: vn=%p\n", vn);
        vn->ops->release(vn);
    }
}

mx_status_t vfs_close(vnode_t* vn) {
    trace(VFS, "vfs_close: vn=%p\n", vn);
    mx_status_t r = vn->ops->close(vn);
    return r;
}
