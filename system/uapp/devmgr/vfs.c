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

static list_node_t iostate_list = LIST_INITIAL_VALUE(iostate_list);
static mtx_t iostate_lock = MTX_INIT;

void track_iostate(iostate_t* ios, const char* fn) {
#if DEBUG_TRACK_NAMES
    if (fn) {
        ios->fn = strdup(fn);
    }
#endif
    mtx_lock(&iostate_lock);
    list_add_tail(&iostate_list, &ios->node);
    mtx_unlock(&iostate_lock);
}

void untrack_iostate(iostate_t* ios) {
    mtx_lock(&iostate_lock);
    list_delete(&ios->node);
    mtx_unlock(&iostate_lock);
#if DEBUG_TRACK_NAMES
    free((void*)ios->fn);
    ios->fn = NULL;
#endif
}

static vnode_t* vfs_root;

// Starting at vnode vn, walk the tree described by the path string,
// until either there is only one path segment remaining in the string
// or we encounter a vnode that represents a remote filesystem
static mx_status_t vfs_walk(vnode_t* vn, vnode_t** out,
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
        if (vn->flags & V_FLAG_REMOTE) {
            // remote filesystem mount, caller must resolve
            xprintf("vfs_walk: vn=%p name='%s' (remote)\n", vn, path);
            *out = vn;
            *pathout = path;
            if (vn->remote > 0) {
                return vn->remote;
            }
            return ERR_NOT_FOUND;
        }
        if ((nextpath = strchr(path, '/')) != NULL) {
            // path has at least one additional segment
            // traverse to the next segment
            len = nextpath - path;
            nextpath++;
            xprintf("vfs_walk: vn=%p name='%.*s' nextpath='%s'\n", vn, (int)len, path, nextpath);
            if ((r = vn->ops->lookup(vn, &vn, path, len))) {
                return r;
            }
            path = nextpath;
        } else {
            // final path segment, we're done here
            xprintf("vfs_walk: vn=%p name='%s' (local)\n", vn, path);
            *out = vn;
            *pathout = path;
            return 0;
        }
    }
    return ERR_NOT_FOUND;
}

static mx_status_t vfs_open(vnode_t* vndir, vnode_t** out,
                            const char* path, const char** pathout,
                            uint32_t flags, uint32_t mode) {
    xprintf("vfs_open: path='%s' flags=%d mode=%x\n", path, flags, mode);
    mx_status_t r;
    if ((r = vfs_walk(vndir, &vndir, path, &path)) < 0) {
        return r;
    }
    if (r > 0) {
        // remote filesystem, return handle and path through to caller
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
            return r;
        }
    } else {
    try_open:
        if ((r = vndir->ops->lookup(vndir, &vn, path, len)) < 0) {
            return r;
        }
        if (vn->remote > 0) {
            *pathout = ".";
            return vn->remote;
        }
        if ((r = vn->ops->open(&vn, flags)) < 0) {
            xprintf("vn open r = %d", r);
            return r;
        }
    }
    *pathout = "";
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
        return ERR_INVALID_ARGS;
    de->size = sz;
    de->type = type;
    memcpy(de->name, name, len);
    de->name[len] = 0;
    return sz;
}

static mx_status_t vfs_get_handles(vnode_t* vn, bool as_dir,
                                   mx_handle_t* hnds, uint32_t* type,
                                   void* extra, uint32_t* esize,
                                   const char* trackfn) {
    mx_status_t r;
    if ((vn->flags & V_FLAG_DEVICE) && !as_dir) {
        // opening a device, get devmgr handles
        uint32_t ids[VFS_MAX_HANDLES];
        r = devmgr_get_handles((mx_device_t*)vn->pdata, NULL, hnds, ids);
        // id 0 == hnds[0] is the real server for cloning this
        // otherwise the type is always rio
        *type = (ids[0] == 0) ? 0 : MXIO_PROTOCOL_REMOTE;
    } else if (vn->flags & V_FLAG_VMOFILE) {
        mx_off_t* args = extra;
        hnds[0] = vfs_get_vmofile(vn, args + 0, args + 1);
        *type = MXIO_PROTOCOL_VMOFILE;
        *esize = sizeof(mx_off_t) * 2;
        r = 1;
    } else {
        // local vnode or device as a directory, we will create the handles
        hnds[0] = vfs_create_handle(vn, trackfn);
        *type = MXIO_PROTOCOL_REMOTE;
        r = 1;
    }
    return r;
}

mx_status_t txn_handoff_clone(mx_handle_t srv, mx_handle_t rh);

static mx_status_t txn_handoff_open(mx_handle_t srv, mx_handle_t rh,
                                    const char* path, uint32_t flags, uint32_t mode) {
    mxrio_msg_t msg;
    memset(&msg, 0, MXRIO_HDR_SZ);
    size_t len = strlen(path);
    msg.op = MXRIO_OPEN;
    msg.arg = flags;
    msg.arg2.mode = mode;
    msg.datalen = len + 1;
    memcpy(msg.data, path, len + 1);
    return mxrio_txn_handoff(srv, rh, &msg);
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

static vnode_t* volatile vfs_txn_vn;
static volatile int vfs_txn_op;

static mx_status_t _vfs_open(mxrio_msg_t* msg, mx_handle_t rh,
                             vnode_t* vn, const char* path,
                             uint32_t flags, uint32_t mode,
                             void* extra, uint32_t* esize) {
    mx_status_t r;
    mtx_lock(&vfs_lock);
    r = vfs_open(vn, &vn, path, &path, flags, mode);
    mtx_unlock(&vfs_lock);
    if (r < 0) {
        xprintf("vfs: open: r=%d\n", r);
        return r;
    }
    if (r > 0) {
        //TODO: unify remote vnodes and remote devices
        //      eliminate vfs_get_handles() and the other
        //      reply pipe path
        if ((r = txn_handoff_open(r, rh, path, flags, mode)) < 0) {
            printf("txn_handoff_open() failed %d\n", r);
            return r;
        }
        return ERR_DISPATCHER_INDIRECT;
    }
    uint32_t type;
    if ((r = vfs_get_handles(vn, flags & O_DIRECTORY, msg->handle, &type,
                             extra, esize, (const char*)msg->data)) < 0) {
        vn->ops->close(vn);
        return r;
    }
    if (type == 0) {
        // device is non-local, handle is the server that
        // can clone it for us, redirect the rpc to there
        if ((r = txn_handoff_clone(msg->handle[0], rh)) < 0) {
            printf("txn_handoff_clone() failed %d\n", r);
            vn_release(vn);
            return r;
        }
        vn_release(vn);
        return ERR_DISPATCHER_INDIRECT;
    }
    // drop the ref from open or create
    // the backend behind get_handles holds the on-going ref
    vn_release(vn);

    msg->arg2.protocol = type;
    msg->hcount = r;
    xprintf("vfs: open: h=%x\n", msg->handle[0]);
    return NO_ERROR;
}

static ssize_t do_ioctl(vnode_t* vn, uint32_t op, const void* in_buf, size_t in_len,
                        void* out_buf, size_t out_len) {
    if (op == IOCTL_DEVICE_WATCH_DIR) {
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
    } else {
        return vn->ops->ioctl(vn, op, in_buf, in_len, out_buf, out_len);
    }
}

static mx_status_t _vfs_handler(mxrio_msg_t* msg, mx_handle_t rh, void* cookie) {
    iostate_t* ios = cookie;
    vnode_t* vn = ios->vn;
    uint32_t len = msg->datalen;
    int32_t arg = msg->arg;
    msg->datalen = 0;

    vfs_txn_vn = vn;
    vfs_txn_op = MXRIO_OP(msg->op);

    for (unsigned i = 0; i < msg->hcount; i++) {
        mx_handle_close(msg->handle[i]);
    }

    switch (MXRIO_OP(msg->op)) {
    case MXRIO_OPEN: {
        char* path = (char*)msg->data;
        if ((len < 1) || (len > 1024)) {
            return ERR_INVALID_ARGS;
        }
        path[len] = 0;
        xprintf("vfs: open name='%s' flags=%d mode=%u\n", path, arg, msg->arg2.mode);
        mx_status_t r = _vfs_open(msg, rh, vn, path, arg, msg->arg2.mode, msg->data, &msg->datalen);
        xprintf("vfs open r=%d dl=%u\n", r, msg->datalen);
        return r;
    }
    case MXRIO_CLOSE:
        // this will drop the ref on the vn
        vn->ops->close(vn);
        untrack_iostate(ios);
        free(ios);
        return NO_ERROR;
    case MXRIO_CLONE:
        if ((msg->handle[0] = vfs_create_handle(vn, "<clone>")) < 0) {
            return msg->handle[0];
        }
        msg->arg2.protocol = MXIO_PROTOCOL_REMOTE;
        msg->hcount = 1;
        return NO_ERROR;
    case MXRIO_READ: {
        ssize_t r = vn->ops->read(vn, msg->data, arg, ios->io_off);
        if (r >= 0) {
            ios->io_off += r;
            msg->arg2.off = ios->io_off;
            msg->datalen = r;
        }
        return r;
    }
    case MXRIO_READ_AT: {
        ssize_t r = vn->ops->read(vn, msg->data, arg, msg->arg2.off);
        if (r >= 0) {
            msg->datalen = r;
        }
        return r;
    }
    case MXRIO_WRITE: {
        ssize_t r = vn->ops->write(vn, msg->data, len, ios->io_off);
        if (r >= 0) {
            ios->io_off += r;
            msg->arg2.off = ios->io_off;
        }
        return r;
    }
    case MXRIO_WRITE_AT: {
        ssize_t r = vn->ops->write(vn, msg->data, len, msg->arg2.off);
        return r;
    }
    case MXRIO_SEEK: {
        vnattr_t attr;
        mx_status_t r;
        if ((r = vn->ops->getattr(vn, &attr)) < 0) {
            return r;
        }
        size_t n;
        switch (arg) {
        case SEEK_SET:
            if (msg->arg2.off < 0) {
                return ERR_INVALID_ARGS;
            }
            n = msg->arg2.off;
            break;
        case SEEK_CUR:
            n = ios->io_off + msg->arg2.off;
            if (msg->arg2.off < 0) {
                // if negative seek
                if (n > ios->io_off) {
                    // wrapped around. attempt to seek before start
                    return ERR_INVALID_ARGS;
                }
            } else {
                // positive seek
                if (n < ios->io_off) {
                    // wrapped around. overflow
                    return ERR_INVALID_ARGS;
                }
            }
            break;
        case SEEK_END:
            n = attr.size + msg->arg2.off;
            if (msg->arg2.off < 0) {
                // if negative seek
                if (n > attr.size) {
                    // wrapped around. attempt to seek before start
                    return ERR_INVALID_ARGS;
                }
            } else {
                // positive seek
                if (n < attr.size) {
                    // wrapped around
                    return ERR_INVALID_ARGS;
                }
            }
            break;
        default:
            return ERR_INVALID_ARGS;
        }
        if (vn->flags & V_FLAG_DEVICE) {
            if (n > attr.size) {
                // devices may not seek past the end
                return ERR_INVALID_ARGS;
            }
        }
        ios->io_off = n;
        msg->arg2.off = ios->io_off;
        return NO_ERROR;
    }
    case MXRIO_STAT: {
        mx_status_t r;
        msg->datalen = sizeof(vnattr_t);
        if ((r = vn->ops->getattr(vn, (vnattr_t*)msg->data)) < 0) {
            return r;
        }
        return msg->datalen;
    }
    case MXRIO_READDIR: {
        if (arg > MXIO_CHUNK_SIZE) {
            return ERR_INVALID_ARGS;
        }
        mx_status_t r;
        mtx_lock(&vfs_lock);
        r = vn->ops->readdir(vn, &ios->dircookie, msg->data, arg);
        mtx_unlock(&vfs_lock);
        if (r >= 0) {
            msg->datalen = r;
        }
        return r;
    }
    case MXRIO_IOCTL: {
        if (len > MXIO_IOCTL_MAX_INPUT) {
            return ERR_INVALID_ARGS;
        }
        char in_buf[MXIO_IOCTL_MAX_INPUT];
        memcpy(in_buf, msg->data, len);

        ssize_t r = do_ioctl(vn, msg->arg2.op, in_buf, len, msg->data, arg);
        if (r >= 0) {
            if (IOCTL_KIND(msg->arg2.op) == IOCTL_KIND_GET_HANDLE) {
                msg->hcount = 1;
                memcpy(msg->handle, msg->data, sizeof(mx_handle_t));
            }
            msg->arg2.off = 0;
            msg->datalen = r;
        }
        return r;
    }
    case MXRIO_RENAME: {
        if (len < 4) { // At least one byte for src + dst + null terminators
            return ERR_INVALID_ARGS;
        }
        char* data_end = (char*)(msg->data + len - 1);
        *data_end = '\0';
        const char* oldpath = (const char*)msg->data;
        size_t oldlen = strlen(oldpath);
        const char* newpath = (const char*)msg->data + (oldlen + 1);
        if (data_end <= newpath) {
            return ERR_INVALID_ARGS;
        }
        vnode_t* oldparent, *newparent;
        mx_status_t r1, r2;
        if ((r1 = vfs_walk(vn, &oldparent, oldpath, &oldpath)) < 0) {
            return r1;
        } else if ((r2 = vfs_walk(vn, &newparent, newpath, &newpath)) < 0) {
            return r2;
        } else if ((r1 != r2) || (r1 == 0) || (r2 == 0)) {
            // Rename can only be directed to one remote filesystem
            return ERR_NOT_SUPPORTED;
        } else if ((r1 = txn_handoff_rename(r1, rh, oldpath, newpath)) < 0) {
            return r1;
        }
        return ERR_DISPATCHER_INDIRECT;
    }
    case MXRIO_UNLINK:
        return vn->ops->unlink(vn, (const char*)msg->data, len);
    default:
        return ERR_NOT_SUPPORTED;
    }
}

static mxio_dispatcher_t* vfs_dispatcher;

static volatile int vfs_txn = -1;
static int vfs_txn_no = 0;

static mx_status_t vfs_handler(mxrio_msg_t* msg, mx_handle_t rh, void* cookie) {
    vfs_txn_no = (vfs_txn_no + 1) & 0x0FFFFFFF;
    vfs_txn = vfs_txn_no;
    mx_status_t r = _vfs_handler(msg, rh, cookie);
    vfs_txn = -1;
    return r;
}

mx_handle_t vfs_create_handle(vnode_t* vn, const char* trackfn) {
    mx_handle_t h[2];
    mx_status_t r;
    iostate_t* ios;

    if ((ios = calloc(1, sizeof(iostate_t))) == NULL)
        return ERR_NO_MEMORY;
    ios->vn = vn;

    if ((r = mx_msgpipe_create(h, 0)) < 0) {
        free(ios);
        return r;
    }
    if ((r = mxio_dispatcher_add(vfs_dispatcher, h[0], vfs_handler, ios)) < 0) {
        mx_handle_close(h[0]);
        mx_handle_close(h[1]);
        free(ios);
        return r;
    }
    track_iostate(ios, trackfn);
    // take a ref for the dispatcher
    vn_acquire(vn);
    return h[1];
}

mx_handle_t vfs_create_root_handle(void) {
    vnode_t* vn = vfs_root;
    mx_status_t r;
    if ((r = vfs_root->ops->open(&vn, O_DIRECTORY)) < 0) {
        return r;
    }
    return vfs_create_handle(vfs_root, "/");
}

static int vfs_watchdog(void* arg) {
    int txn = vfs_txn;
    for (;;) {
        mx_nanosleep(1000000000ULL);
        int now = vfs_txn;
        if ((now == txn) && (now != -1)) {
            vnode_t* vn = vfs_txn_vn;
            printf("devmgr: watchdog: txn %d did not complete: vn=%p op=%d\n", txn, vn, vfs_txn_op);
            if (vn->flags & V_FLAG_DEVICE) {
                printf("devmgr: watchdog: vn=%p is device '%s'\n", vn,
                       ((mx_device_t*)vn->pdata)->name);
            }
        }
        txn = now;
    }
    return 0;
}

void vfs_init(vnode_t* root) {
    vfs_root = root;
    if (mxio_dispatcher_create(&vfs_dispatcher, mxrio_handler) == NO_ERROR) {
        mxio_dispatcher_start(vfs_dispatcher, "vfs-rio-dispatcher");
    }
    thrd_t t;
    thrd_create_with_name(&t, vfs_watchdog, NULL, "vfs-watchdog");
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
    iostate_t* ios;
    mtx_lock(&iostate_lock);
    list_for_every_entry (&iostate_list, ios, iostate_t, node) {
        printf("obj %p '%s'\n", ios->vn, ios->fn ? ios->fn : "???");
    }
    mtx_unlock(&iostate_lock);
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
