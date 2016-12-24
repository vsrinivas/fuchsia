// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/listnode.h>

#include <mxio/debug.h>
#include <mxio/dispatcher.h>
#include <mxio/io.h>
#include <mxio/remoteio.h>
#include <mxio/vfs.h>

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <sys/stat.h>

#include "vfs-internal.h"


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

static mx_status_t txn_handoff_clone(mx_handle_t srv, mx_handle_t rh) {
    mxrio_msg_t msg;
    memset(&msg, 0, MXRIO_HDR_SZ);
    msg.op = MXRIO_CLONE;
    return mxrio_txn_handoff(srv, rh, &msg);
}

// Initializes io state for a vnode and attaches it to a dispatcher.
static mx_status_t vfs_rpc_open(mxrio_msg_t* msg, mx_handle_t rh, vnode_t* vn,
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
    uint32_t* type = &msg->arg2.protocol;
    if ((r = vfs_get_handles(vn, flags, msg->handle, type, msg->data, &msg->datalen)) < 0) {
        vn->ops->close(vn);
        return r;
    }
    if (*type == 0) {
        // device is non-local, handle is the server that
        // can clone it for us, redirect the rpc to there
        if ((r = txn_handoff_open(msg->handle[0], rh, ".", flags, mode)) < 0) {
            printf("txn_handoff_open() failed %d\n", r);
            vn_release(vn);
            return r;
        }
        vn_release(vn);
        return ERR_DISPATCHER_INDIRECT;
    }
    // drop the ref from vfs_open
    // the backend behind get_handles holds the on-going ref
    vn_release(vn);

    msg->hcount = r;
    xprintf("vfs: open: h=%x\n", msg->handle[0]);
    return NO_ERROR;
}

mx_handle_t vfs_create_handle(vnode_t* vn, uint32_t flags) {
    mx_handle_t h[2];
    mx_status_t r;
    vfs_iostate_t* ios;

    if ((ios = calloc(1, sizeof(vfs_iostate_t))) == NULL)
        return ERR_NO_MEMORY;
    ios->vn = vn;
    ios->io_flags = flags;

    if ((r = mx_channel_create(0, &h[0], &h[1])) < 0) {
        free(ios);
        return r;
    }
    if ((r = mxio_dispatcher_add(vfs_dispatcher, h[0], vfs_handler, ios)) < 0) {
        mx_handle_close(h[0]);
        mx_handle_close(h[1]);
        free(ios);
        return r;
    }
    // take a ref for the dispatcher
    vn_acquire(vn);
    return h[1];
}

mx_status_t vfs_handler_generic(mxrio_msg_t* msg, mx_handle_t rh, void* cookie) {
    vfs_iostate_t* ios = cookie;
    vnode_t* vn = ios->vn;
    uint32_t len = msg->datalen;
    int32_t arg = msg->arg;
    msg->datalen = 0;

    for (unsigned i = 0; i < msg->hcount; i++) {
        mx_handle_close(msg->handle[i]);
    }
    msg->hcount = 0;

    switch (MXRIO_OP(msg->op)) {
    case MXRIO_OPEN: {
        char* path = (char*)msg->data;
        if ((len < 1) || (len > 1024)) {
            return ERR_INVALID_ARGS;
        }
        path[len] = 0;
        xprintf("vfs: open name='%s' flags=%d mode=%u\n", path, arg, msg->arg2.mode);
        mx_status_t r = vfs_rpc_open(msg, rh, vn, path, arg, msg->arg2.mode);
        xprintf("vfs open r=%d dl=%u\n", r, msg->datalen);
        return r;
    }
    case MXRIO_CLOSE:
        // this will drop the ref on the vn
        vfs_close(vn);
        free(ios);
        return NO_ERROR;
    case MXRIO_CLONE:
        if ((msg->handle[0] = vfs_create_handle(vn, ios->io_flags)) < 0) {
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
    case MXRIO_SETATTR: {
        mx_status_t r = vn->ops->setattr(vn, (vnattr_t*)msg->data);
        return r;
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
            switch (IOCTL_KIND(msg->arg2.op)) {
                case IOCTL_KIND_DEFAULT:
                    break;
                case IOCTL_KIND_GET_HANDLE:
                    msg->hcount = 1;
                    memcpy(msg->handle, msg->data, sizeof(mx_handle_t));
                    break;
                case IOCTL_KIND_GET_TWO_HANDLES:
                    msg->hcount = 2;
                    memcpy(msg->handle, msg->data, 2 * sizeof(mx_handle_t));
                    break;
                case IOCTL_KIND_GET_THREE_HANDLES:
                    msg->hcount = 3;
                    memcpy(msg->handle, msg->data, 3 * sizeof(mx_handle_t));
                    break;
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
    case MXRIO_SYNC: {
        return vn->ops->sync(vn);
    }
    case MXRIO_UNLINK:
        return vfs_unlink(vn, (const char*)msg->data, len);
    default:
        return ERR_NOT_SUPPORTED;
    }
}
