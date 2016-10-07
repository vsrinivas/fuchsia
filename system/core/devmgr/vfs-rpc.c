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

static mxio_dispatcher_t* vfs_dispatcher;

static mx_status_t vfs_handler(mxrio_msg_t* msg, mx_handle_t rh, void* cookie);

// Initializes io state for a vnode, and attaches it to the vfs dispatcher
static mx_handle_t vfs_create_handle(vnode_t* vn, const char* trackfn, uint32_t flags) {
    mx_handle_t h[2];
    mx_status_t r;
    vfs_iostate_t* ios;

    if ((ios = calloc(1, sizeof(vfs_iostate_t))) == NULL)
        return ERR_NO_MEMORY;
    ios->vn = vn;
    ios->io_flags = flags;

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
    track_vfs_iostate(ios, trackfn);
    // take a ref for the dispatcher
    vn_acquire(vn);
    return h[1];
}

static mx_handle_t devmgr_connect(const char* where) {
    int fd;
    if ((fd = open(where, O_DIRECTORY | O_RDWR)) < 0) {
        printf("memfs: cannot open '%s'\n", where);
        return -1;
    }
    mx_handle_t h;
    if (ioctl_devmgr_mount_fs(fd, &h) != sizeof(h)) {
        close(fd);
        printf("memfs: failed to attach to '%s'\n", where);
        return -1;
    }
    close(fd);
    printf("memfs: mounted at '%s'\n", where);
    return h;
}

// TODO(smklein): No one is calling this yet...
// Assumes vfs_dispatcher is already running
mx_handle_t vfs_rpc_mount(vnode_t* vn, const char* where) {
    vfs_iostate_t* ios;
    mx_status_t r;

    if ((ios = calloc(1, sizeof(vfs_iostate_t))) == NULL)
        return ERR_NO_MEMORY;
    ios->vn = vn;

    mx_handle_t h;
    if ((h = devmgr_connect(where)) < 0) {
        free(ios);
        return h;
    }

    if ((r = mxio_dispatcher_add(vfs_dispatcher, h, vfs_handler, ios)) < 0) {
        free(ios);
        mx_handle_close(h);
        return r;
    }
    return NO_ERROR;
}

static mx_status_t vfs_get_handles(vnode_t* vn, uint32_t flags,
                                   mx_handle_t* hnds, uint32_t* type,
                                   void* extra, uint32_t* esize,
                                   const char* trackfn) {
    if ((vn->flags & V_FLAG_DEVICE) && !(flags & O_DIRECTORY)) {
        *type = 0;
        hnds[0] = vn->remote;
        return 1;
    } else if (vn->flags & V_FLAG_VMOFILE) {
        mx_off_t* args = extra;
        hnds[0] = vfs_get_vmofile(vn, args + 0, args + 1);
        *type = MXIO_PROTOCOL_VMOFILE;
        *esize = sizeof(mx_off_t) * 2;
        return 1;
    } else {
        // local vnode or device as a directory, we will create the handles
        hnds[0] = vfs_create_handle(vn, trackfn, flags);
        *type = MXIO_PROTOCOL_REMOTE;
        return 1;
    }
}

mx_status_t txn_handoff_clone(mx_handle_t srv, mx_handle_t rh) {
    mxrio_msg_t msg;
    memset(&msg, 0, MXRIO_HDR_SZ);
    msg.op = MXRIO_CLONE;
    return mxrio_txn_handoff(srv, rh, &msg);
}

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

static mx_status_t _vfs_open(mxrio_msg_t* msg, mx_handle_t rh, vnode_t* vn,
                             const char* path, uint32_t flags, uint32_t mode) {
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
    if ((r = vfs_get_handles(vn, flags, msg->handle, &type,
                             msg->data, &msg->datalen, (const char*)msg->data)) < 0) {
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

static vnode_t* volatile vfs_txn_vn;
static volatile int vfs_txn_op;
static volatile int vfs_txn = -1;
static int vfs_txn_no = 0;

static mx_status_t _vfs_handler(mxrio_msg_t* msg, mx_handle_t rh, void* cookie) {
    vfs_iostate_t* ios = cookie;
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
        mx_status_t r = _vfs_open(msg, rh, vn, path, arg, msg->arg2.mode);
        xprintf("vfs open r=%d dl=%u\n", r, msg->datalen);
        return r;
    }
    case MXRIO_CLOSE:
        // this will drop the ref on the vn
        vn->ops->close(vn);
        untrack_vfs_iostate(ios);
        free(ios);
        return NO_ERROR;
    case MXRIO_CLONE:
        if ((msg->handle[0] = vfs_create_handle(vn, "<clone>", ios->io_flags)) < 0) {
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
        if (ios->io_flags & O_APPEND) {
            vnattr_t attr;
            mx_status_t r;
            if ((r = vn->ops->getattr(vn, &attr)) < 0) {
                return r;
            }
            ios->io_off = attr.size;
        }
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

        ssize_t r = vfs_do_ioctl(vn, msg->arg2.op, in_buf, len, msg->data, arg);
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
    case MXRIO_TRUNCATE: {
        if (msg->arg2.off < 0) {
            return ERR_INVALID_ARGS;
        }
        return vn->ops->truncate(vn, msg->arg2.off);
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
        return vfs_rename(vn, oldpath, newpath, rh);
    }
    case MXRIO_UNLINK:
        return vn->ops->unlink(vn, (const char*)msg->data, len);
    default:
        return ERR_NOT_SUPPORTED;
    }
}

static mx_status_t vfs_handler(mxrio_msg_t* msg, mx_handle_t rh, void* cookie) {
    vfs_txn_no = (vfs_txn_no + 1) & 0x0FFFFFFF;
    vfs_txn = vfs_txn_no;
    mx_status_t r = _vfs_handler(msg, rh, cookie);
    vfs_txn = -1;
    return r;
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

// Acquire the root vnode and return a handle to it through the VFS dispatcher
mx_handle_t vfs_create_root_handle(vnode_t* vn) {
    mx_status_t r;
    if ((r = vn->ops->open(&vn, O_DIRECTORY)) < 0) {
        return r;
    }
    return vfs_create_handle(vn, "/", 0);
}

static vnode_t* global_vfs_root;

// Initialize the global root VFS node and dispatcher
void vfs_global_init(vnode_t* root) {
    global_vfs_root = root;
    if (mxio_dispatcher_create(&vfs_dispatcher, mxrio_handler) == NO_ERROR) {
        mxio_dispatcher_start(vfs_dispatcher, "vfs-rio-dispatcher");
    }
    thrd_t t;
    thrd_create_with_name(&t, vfs_watchdog, NULL, "vfs-watchdog");
}

// Return a RIO handle to the global root
mx_handle_t vfs_create_global_root_handle() {
    return vfs_create_root_handle(global_vfs_root);
}
