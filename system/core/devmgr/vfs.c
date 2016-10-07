// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vfs.h"
#include "devmgr.h"
#include "dnode.h"

#include <mxio/debug.h>
#include <mxio/dispatcher.h>
#include <mxio/io.h>
#include <mxio/remoteio.h>

#include <ddk/device.h>

#include <magenta/listnode.h>

#include <magenta/device/device.h>
#include <magenta/device/devmgr.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#define MXDEBUG 0

#define DEBUG_TRACK_NAMES 1

mtx_t vfs_lock = MTX_INIT;

static list_node_t vfs_iostate_list = LIST_INITIAL_VALUE(vfs_iostate_list);
static mtx_t vfs_iostate_lock = MTX_INIT;

void track_vfs_iostate(vfs_iostate_t* ios, const char* fn) {
#if DEBUG_TRACK_NAMES
    if (fn) {
        ios->fn = strdup(fn);
    }
#endif
    mtx_lock(&vfs_iostate_lock);
    list_add_tail(&vfs_iostate_list, &ios->node);
    mtx_unlock(&vfs_iostate_lock);
}

void untrack_vfs_iostate(vfs_iostate_t* ios) {
    mtx_lock(&vfs_iostate_lock);
    list_delete(&ios->node);
    mtx_unlock(&vfs_iostate_lock);
#if DEBUG_TRACK_NAMES
    free((void*)ios->fn);
    ios->fn = NULL;
#endif
}

// Starting at vnode vn, walk the tree described by the path string,
// until either there is only one path segment remaining in the string
// or we encounter a vnode that represents a remote filesystem
//
// If a non-negative status is returned, the vnode at 'out' has been acquired.
// Otherwise, no net deltas in acquires/releases occur.
static mx_status_t vfs_walk(vnode_t* vn, vnode_t** out,
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
        if (vn->flags & V_FLAG_REMOTE) {
            // remote filesystem mount, caller must resolve
            xprintf("vfs_walk: vn=%p name='%s' (remote)\n", vn, path);
            *out = vn;
            *pathout = path;
            if (oldvn == NULL) {
                // returning our original vnode, need to upref it
                vn_acquire(vn);
            }
            if (vn->remote > 0) {
                return vn->remote;
            }
            return ERR_NOT_FOUND;
        } else if ((nextpath = strchr(path, '/')) != NULL) {
            // path has at least one additional segment
            // traverse to the next segment
            len = nextpath - path;
            nextpath++;
            xprintf("vfs_walk: vn=%p name='%.*s' nextpath='%s'\n", vn, (int)len, path, nextpath);
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
            xprintf("vfs_walk: vn=%p name='%s' (local)\n", vn, path);
            if (oldvn == NULL) {
                // returning our original vnode, need to upref it
                vn_acquire(vn);
            }
            *out = vn;
            *pathout = path;
            return NO_ERROR;
        }
    }
}

mx_status_t vfs_open(vnode_t* vndir, vnode_t** out, const char* path,
                     const char** pathout, uint32_t flags, uint32_t mode) {
    xprintf("vfs_open: path='%s' flags=%d mode=%x\n", path, flags, mode);
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
        if ((vn->flags & V_FLAG_REMOTE) && (vn->remote > 0)) {
            *pathout = ".";
            r = vn->remote;
            vn_release(vn);
            return r;
        }
        r = vn->ops->open(&vn, flags);
        // Open and lookup both incremented the refcount. Release it once for
        // opening a vnode.
        vn_release(vn);
        if (r < 0) {
            xprintf("vn open r = %d", r);
            return r;
        }
        if (flags & O_TRUNC) {
            if ((r = vn->ops->truncate(vn, 0)) < 0) {
                vn_release(vn);
                return r;
            }
        }
    }
    *pathout = "";
    *out = vn;
    return NO_ERROR;
}

static mx_status_t txn_handoff_rename(mx_handle_t srv, mx_handle_t rh,
                                      const char* oldpath, const char* newpath) {
    mxrio_msg_t msg;
    memset(&msg, 0, MXRIO_HDR_SZ);
    size_t oldlen = strlen(oldpath);
    size_t newlen = strlen(newpath);
    msg.op = MXRIO_RENAME;
    memcpy(msg.data, oldpath, oldlen);
    msg.data[oldlen] = '\0';
    memcpy(msg.data + oldlen + 1, newpath, newlen);
    msg.data[oldlen + newlen + 1] = '\0';
    msg.datalen = oldlen + newlen + 2;
    return mxrio_txn_handoff(srv, rh, &msg);
}

mx_status_t vfs_rename(vnode_t* vn, const char* oldpath, const char* newpath,
                       mx_handle_t rh) {
    vnode_t* oldparent, *newparent;
    mx_status_t r, r_old, r_new;
    if ((r_old = vfs_walk(vn, &oldparent, oldpath, &oldpath)) < 0) {
        return r_old;
    } else if ((r_new = vfs_walk(vn, &newparent, newpath, &newpath)) < 0) {
        vn_release(oldparent);
        return r_new;
    } else if (r_old != r_new) {
        // Rename can only be directed to one filesystem
        vn_release(oldparent);
        vn_release(newparent);
        return ERR_NOT_SUPPORTED;
    }

    if (r_old == 0) {
        // Local filesystem
        r = vn->ops->rename(oldparent, newparent, oldpath, strlen(oldpath),
                            newpath, strlen(newpath));
    } else {
        // Remote filesystem.
        r = txn_handoff_rename(r_old, rh, oldpath, newpath);
        if (r >= 0) {
            r = ERR_DISPATCHER_INDIRECT;
        }
    }

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
    case IOCTL_DEVICE_WATCH_DIR: {
        if ((out_len != sizeof(mx_handle_t)) || (in_len != 0)) {
            return ERR_INVALID_ARGS;
        }
        if (vn->dnode == NULL) {
            // not a directory
            return ERR_WRONG_TYPE;
        }
        vnode_watcher_t* watcher;
        if ((watcher = calloc(1, sizeof(vnode_watcher_t))) == NULL) {
            return ERR_NO_MEMORY;
        }
        mx_handle_t h[2];
        if (mx_msgpipe_create(h, 0) < 0) {
            free(watcher);
            return ERR_NO_RESOURCES;
        }
        watcher->h = h[1];
        memcpy(out_buf, h, sizeof(mx_handle_t));
        mtx_lock(&vfs_lock);
        list_add_tail(&vn->watch_list, &watcher->node);
        mtx_unlock(&vfs_lock);
        xprintf("new watcher vn=%p w=%p\n", vn, watcher);
        return sizeof(mx_handle_t);
    }
    case IOCTL_DEVMGR_MOUNT_FS: {
        if ((in_len != 0) || (out_len != sizeof(mx_handle_t))) {
            return ERR_INVALID_ARGS;
        }
        mx_handle_t h[2];
        mx_status_t status;
        if ((status = mx_msgpipe_create(h, 0)) < 0) {
            return status;
        }
        if ((status = vfs_install_remote(vn, h[1])) < 0) {
            mx_handle_close(h[0]);
            mx_handle_close(h[1]);
            return status;
        }
        memcpy(out_buf, h, sizeof(mx_handle_t));
        return sizeof(mx_handle_t);
    }
    default:
        return vn->ops->ioctl(vn, op, in_buf, in_len, out_buf, out_len);
    }
}

void vn_acquire(vnode_t* vn) {
    vn->refcount++;
}

void vn_release(vnode_t* vn) {
    if (vn->refcount == 0) {
        printf("vn %p: ref underflow\n", vn);
        panic();
    }
    vn->refcount--;
    if (vn->refcount == 0) {
        vn->ops->release(vn);
    }
}

void vfs_dump_handles(void) {
    vfs_iostate_t* ios;
    mtx_lock(&vfs_iostate_lock);
    list_for_every_entry (&vfs_iostate_list, ios, vfs_iostate_t, node) {
        printf("obj %p '%s'\n", ios->vn, ios->fn ? ios->fn : "???");
    }
    mtx_unlock(&vfs_iostate_lock);
}

void vfs_notify_add(vnode_t* vn, const char* name, size_t len) {
    xprintf("devfs: notify vn=%p name='%.*s'\n", vn, (int)len, name);
    vnode_watcher_t* watcher;
    vnode_watcher_t* tmp;
    list_for_every_entry_safe (&vn->watch_list, watcher, tmp, vnode_watcher_t, node) {
        mx_status_t status;
        if ((status = mx_msgpipe_write(watcher->h, name, len, NULL, 0, 0)) < 0) {
            xprintf("devfs: watcher %p write failed %d\n", watcher, status);
            list_delete(&watcher->node);
            mx_handle_close(watcher->h);
            free(watcher);
        } else {
            xprintf("devfs: watcher %p notified\n", watcher);
        }
    }
}
