// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mxio/dispatcher.h>
#include <mxio/io.h>
#include <mxio/remoteio.h>
#include <mxio/vfs.h>

#include <magenta/device/devmgr.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>

#include "vfs.h"

struct vnode {
    VNODE_BASE_FIELDS
};

typedef struct iostate {
    vnode_t* vn;
    vdircookie_t dircookie;
    size_t io_off;
    uint32_t io_flags;
} iostate_t;

static mxio_dispatcher_t* vfs_dispatcher;

static mx_status_t vfs_handler(mxrio_msg_t* msg, mx_handle_t rh, void* cookie);

static mx_handle_t vfs_create_handle(vnode_t* vn, uint32_t flags) {
    mx_handle_t h[2];
    mx_status_t r;
    iostate_t* ios;

    if ((ios = calloc(1, sizeof(iostate_t))) == NULL)
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
    // take a ref for the dispatcher
    vn_acquire(vn);
    return h[1];
}

static mx_handle_t devmgr_connect(const char* where) {
    int fd;
    if ((fd = open(where, O_DIRECTORY | O_RDWR)) < 0) {
        error("minfs: cannot open '%s'\n", where);
        return -1;
    }
    mx_handle_t h;
    if (ioctl_devmgr_mount_fs(fd, &h) != sizeof(h)) {
        close(fd);
        error("minfs: failed to attach to '%s'\n", where);
        return -1;
    }
    close(fd);
    trace(RPC, "minfs: mounted at '%s'\n", where);
    return h;
}

mx_handle_t vfs_rpc_server(vnode_t* vn, const char* where) {
    iostate_t* ios;
    mx_status_t r;

    if ((ios = calloc(1, sizeof(iostate_t))) == NULL)
        return ERR_NO_MEMORY;
    ios->vn = vn;

    if ((r = mxio_dispatcher_create(&vfs_dispatcher, mxrio_handler)) < 0) {
        free(ios);
        return r;
    }

    mx_handle_t h;
    if ((h = devmgr_connect(where)) < 0) {
        //TODO: proper cleanup when possible
        //mxio_dispatcher_destroy(&vfs_dispatcher);
        return h;
    }

    if ((r = mxio_dispatcher_add(vfs_dispatcher, h, vfs_handler, ios)) < 0) {
        free(ios);
        return r;
    }
    //TODO: ref count
    //vn_acquire(vn);
    mxio_dispatcher_run(vfs_dispatcher);
    return NO_ERROR;
}

static mx_status_t vfs_handler(mxrio_msg_t* msg, mx_handle_t rh, void* cookie) {
    iostate_t* ios = cookie;
    vnode_t* vn = ios->vn;
    uint32_t len = msg->datalen;
    int32_t arg = msg->arg;
    msg->datalen = 0;

    for (unsigned i = 0; i < msg->hcount; i++) {
        mx_handle_close(msg->handle[i]);
    }

    trace(RPC, "rpc: op=%x, vn=%p\n", msg->op, vn);

    switch (MXRIO_OP(msg->op)) {
    case MXRIO_OPEN: {
        char* path = (char*)msg->data;
        if ((len < 1) || (len > 1024)) {
            return ERR_INVALID_ARGS;
        }
        path[len] = 0;
        mx_status_t r;
        if ((r = vfs_open(vn, &vn, path, arg, msg->arg2.mode)) < 0) {
            return r;
        }
        if ((msg->handle[0] = vfs_create_handle(vn, arg)) < 0) {
            vfs_close(vn);
            return msg->handle[0];
        }

        // release the ref from open, vfs_create_handle() takes
        // its own ref on success
        vn_release(vn);

        msg->arg2.protocol = MXIO_PROTOCOL_REMOTE;
        msg->hcount = 1;
        return NO_ERROR;
    }
    case MXRIO_CLONE:
        if ((msg->handle[0] = vfs_create_handle(vn, ios->io_flags)) < 0) {
            return msg->handle[0];
        }
        msg->arg2.protocol = MXIO_PROTOCOL_REMOTE;
        msg->hcount = 1;
        return NO_ERROR;
    case MXRIO_CLOSE:
        // this will drop the ref on the vn
        vfs_close(vn);
        free(ios);
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
        if ((r = vn->ops->readdir(vn, &ios->dircookie, msg->data, arg)) >= 0) {
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

        ssize_t r = vn->ops->ioctl(vn, msg->arg2.op, in_buf, len, msg->data, arg);
        if (r >= 0) {
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
        return vfs_rename(vn, oldpath, newpath);
    }
    case MXRIO_UNLINK:
        return vn->ops->unlink(vn, (const char*)msg->data, len);
    default:
        return ERR_NOT_SUPPORTED;
    }
}
