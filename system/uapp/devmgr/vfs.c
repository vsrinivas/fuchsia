// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "vfs.h"

#include <mxio/debug.h>
#include <mxio/dispatcher.h>
#include <mxio/io.h>
#include <mxio/remoteio.h>

#include <ddk/device.h>

#include <mxu/list.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>

#include <runtime/thread.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MXDEBUG 0

static vnode_t* vfs_root;

// Starting at vnode vn, walk the tree described by the path string,
// returning the final vnode via out on success.
//
// If nameout is non-NULL, the final segment of the path will not
// be traversed (useful for stopping at the directory node for a
// create() op or the like)
mx_status_t vfs_walk(vnode_t* vn, vnode_t** out, const char* path, const char** nameout) {
    const char* nextpath;
    mx_status_t r;
    size_t len;

    for (;;) {
        if ((nextpath = strchr(path, '/')) != NULL) {
            len = nextpath - path;
            nextpath++;
            xprintf("vfs_walk: vn=%p name='%.*s' nextpath='%s'\n", vn, (int)len, path, nextpath);
            if ((r = vn->ops->lookup(vn, &vn, path, len))) {
                return r;
            }
            path = nextpath;
        } else {
            xprintf("vfs_walk: vn=%p name='%s'\n", vn, path);
            if (nameout != NULL) {
                *out = vn;
                *nameout = path;
                return 0;
            }
            return vn->ops->lookup(vn, out, path, strlen(path));
        }
    }
    return ERR_NOT_FOUND;
}

mx_status_t vfs_open(vnode_t** out, const char* path, uint32_t flags) {
    if ((out == NULL) || (path == NULL) || (path[0] != '/')) {
        return ERR_INVALID_ARGS;
    }
    if (path[1] == 0) {
        vn_acquire(vfs_root);
        *out = vfs_root;
        return NO_ERROR;
    }
    vnode_t* vn;
    mx_status_t r;
    if ((r = vfs_walk(vfs_root, &vn, path + 1, NULL)) < 0) {
        return r;
    }
    if ((r = vn->ops->open(&vn, flags)) < 0) {
        xprintf("vn open r = %d", r);
        return r;
    }
    *out = vn;
    return NO_ERROR;
}

mx_status_t vfs_create(vnode_t** out, const char* path, uint32_t flags, uint32_t mode) {
    if ((out == NULL) || (path == NULL) || (path[0] != '/')) {
        xprintf("vfs_create: bogus args\n");
        return ERR_INVALID_ARGS;
    }
    if (path[1] == 0) {
        return ERR_NOT_ALLOWED;
    }
    xprintf("vfs_create: path='%s' flags=%d mode=%d\n", path, flags, mode);
    vnode_t* vn;
    mx_status_t r;
    if ((r = vfs_walk(vfs_root, &vn, path + 1, &path)) < 0) {
        xprintf("vfs_create: walk r=%d\n", r);
        return r;
    }
    r = vn->ops->create(vn, out, path, strlen(path), mode);
    xprintf("vfs_create: create r=%d\n", r);
    return r;
}

mx_status_t vfs_fill_dirent(vdirent_t* de, size_t delen,
                            const char* name, size_t len, uint32_t type) {
    size_t sz = sizeof(vdirent_t) + len + 1;

    // round up to uint32 aligned
    if (sz & 3)
        sz = (sz + 3) & (~3);
    if (sz > delen)
        return ERR_TOO_BIG;
    de->size = sz;
    de->type = type;
    memcpy(de->name, name, len);
    de->name[len] = 0;
    return sz;
}

static mx_status_t vfs_get_handles(vnode_t* vn, mx_handle_t* hnds, uint32_t* ids) {
    mx_status_t r;
    if ((r = vn->ops->gethandles(vn, hnds, ids)) == ERR_NOT_SUPPORTED) {
        // local vnode, we will create the handles
        hnds[0] = vfs_create_handle(vn);
        ids[0] = MX_HND_TYPE_MXIO_REMOTE;
        r = 1;
    }
    return r;
}

mx_status_t vfs_open_handles(mx_handle_t* hnds, uint32_t* ids, uint32_t arg,
                             const char* path, uint32_t flags) {
    mx_status_t r;
    vnode_t* vn;

    if (flags & O_CREAT) {
        return ERR_NOT_SUPPORTED;
    }
    if ((r = vfs_open(&vn, path, flags)) < 0) {
        return r;
    }
    if ((r = vfs_get_handles(vn, hnds, ids)) < 0) {
        return r;
    }
    for (int i = 0; i < r; i++) {
        ids[i] |= (arg & 0xFFFF) << 16;
    }
    return r;
}

// remoteio transport wrapper
static mx_status_t root_handler(mx_rio_msg_t* msg, void* cookie) {
    uint32_t len = msg->datalen;
    int32_t arg = msg->arg;
    mx_status_t r;
    vnode_t* vn;

    msg->datalen = 0;

    for (unsigned i = 0; i < msg->hcount; i++) {
        _magenta_handle_close(msg->handle[i]);
    }

    switch (MX_RIO_OP(msg->op)) {
    case MX_RIO_OPEN: {
        if ((len < 1) || (len > 1024)) {
            return ERR_INVALID_ARGS;
        }
        msg->data[len] = 0;
        xprintf("root: open name='%s' flags=%d mode=%lld\n", (const char*)msg->data, arg, msg->off);
        if (arg & O_CREAT) {
            r = vfs_create(&vn, (const char*)msg->data, arg, msg->off);
        } else {
            r = vfs_open(&vn, (const char*)msg->data, arg);
        }
        xprintf("root: open: r=%d\n", r);
        if (r < 0) {
            return r;
        }
        uint32_t ids[VFS_MAX_HANDLES];
        if ((r = vfs_get_handles(vn, msg->handle, ids)) < 0) {
            return r;
        }
        // TODO: ensure this is always true:
        msg->off = MXIO_PROTOCOL_REMOTE;
        msg->hcount = r;
        xprintf("root: open: h=%x\n", msg->handle[0]);
        return NO_ERROR;
    }
    case MX_RIO_CLONE:
        msg->off = MXIO_PROTOCOL_REMOTE;
        msg->hcount = 1;
        if ((msg->handle[0] = vfs_create_root_handle()) < 0) {
            return msg->handle[0];
        } else {
            return NO_ERROR;
        }
    case MX_RIO_CLOSE:
        return NO_ERROR;
    default:
        return ERR_NOT_SUPPORTED;
    }
}

typedef struct iostate iostate_t;
struct iostate {
    vnode_t* vn;
    size_t io_off;
    vdircookie_t cookie;
};

static vnode_t* volatile vfs_txn_vn;
static volatile int vfs_txn_op;

static mx_status_t _vfs_handler(mx_rio_msg_t* msg, void* cookie) {
    iostate_t* ios = cookie;
    vnode_t* vn = ios->vn;
    uint32_t len = msg->datalen;
    int32_t arg = msg->arg;
    msg->datalen = 0;

    vfs_txn_vn = vn;
    vfs_txn_op = MX_RIO_OP(msg->op);

    for (unsigned i = 0; i < msg->hcount; i++) {
        _magenta_handle_close(msg->handle[i]);
    }

    switch (MX_RIO_OP(msg->op)) {
    case MX_RIO_CLOSE:
        vn->ops->close(vn);
        free(ios);
        return NO_ERROR;
    case MX_RIO_CLONE: {
        uint32_t ids[VFS_MAX_HANDLES];
        mx_status_t r = vfs_get_handles(vn, msg->handle, ids);
        if (r < 0) {
            return r;
        }
        // TODO: ensure this is always true:
        msg->off = MXIO_PROTOCOL_REMOTE;
        msg->hcount = r;
        return NO_ERROR;
    }
    case MX_RIO_READ: {
        ssize_t r = vn->ops->read(vn, msg->data, arg, ios->io_off);
        if (r >= 0) {
            ios->io_off += r;
            msg->off = ios->io_off;
            msg->datalen = r;
        }
        return r;
    }
    case MX_RIO_WRITE: {
        ssize_t r = vn->ops->write(vn, msg->data, len, ios->io_off);
        if (r >= 0) {
            ios->io_off += r;
            msg->off = ios->io_off;
        }
        return r;
    }
    case MX_RIO_SEEK:
        switch (arg) {
        case SEEK_SET:
            ios->io_off = msg->off;
            break;
        case SEEK_CUR:
            break;
        case SEEK_END: {
            vnattr_t attr;
            mx_status_t r;
            if ((r = vn->ops->getattr(vn, &attr)) < 0)
                return r;
            ios->io_off = attr.size;
            break;
        }
        default:
            return ERR_INVALID_ARGS;
        }
        msg->off = ios->io_off;
        return NO_ERROR;
    case MX_RIO_STAT: {
        mx_status_t r;
        msg->datalen = sizeof(vnattr_t);
        if ((r = vn->ops->getattr(vn, (vnattr_t*)msg->data)) < 0) {
            return r;
        }
        return msg->datalen;
    }
    case MX_RIO_READDIR: {
        if (arg > MXIO_CHUNK_SIZE) {
            return ERR_INVALID_ARGS;
        }
        mx_status_t r = vn->ops->readdir(vn, &ios->cookie, msg->data, arg);
        if (r >= 0) {
            msg->datalen = r;
        }
        return r;
    }
    case MX_RIO_IOCTL: {
        if (!vn->ops->ioctl) {
            return ERR_NOT_SUPPORTED;
        }
        if (len > MXIO_IOCTL_MAX_INPUT) {
            return ERR_INVALID_ARGS;
        }
        char in_buf[MXIO_IOCTL_MAX_INPUT];
        memcpy(in_buf, msg->data, len);

        ssize_t r = vn->ops->ioctl(vn, msg->off, in_buf, len, msg->data, arg);
        if (r >= 0) {
            msg->off = 0;
            msg->datalen = r;
        }
        return r;
    }
    default:
        return ERR_NOT_SUPPORTED;
    }
}

static mxio_dispatcher_t* vfs_dispatcher;

mx_handle_t vfs_create_root_handle(void) {
    mx_handle_t h0, h1;
    mx_status_t r;
    if ((h0 = _magenta_message_pipe_create(&h1)) < 0)
        return h0;
    if ((r = mxio_dispatcher_add(vfs_dispatcher, h0, root_handler, NULL))) {
        _magenta_handle_close(h0);
        _magenta_handle_close(h1);
        return r;
    }
    return h1;
}

static volatile int vfs_txn = -1;
static int vfs_txn_no = 0;

static mx_status_t vfs_handler(mx_rio_msg_t* msg, void* cookie) {
    vfs_txn_no = (vfs_txn_no + 1) & 0x0FFFFFFF;
    vfs_txn = vfs_txn_no;
    mx_status_t r = _vfs_handler(msg, cookie);
    vfs_txn = -1;
    return r;
}

mx_handle_t vfs_create_handle(vnode_t* vn) {
    mx_handle_t h0, h1;
    mx_status_t r;
    iostate_t* ios;

    if ((ios = calloc(1, sizeof(iostate_t))) == NULL)
        return ERR_NO_MEMORY;
    ios->vn = vn;

    if ((h0 = _magenta_message_pipe_create(&h1)) < 0) {
        free(ios);
        return h0;
    }
    if (vn->flags & V_FLAG_BLOCKING) {
        r = mxio_handler_create(h0, vfs_handler, ios);
    } else {
        r = mxio_dispatcher_add(vfs_dispatcher, h0, vfs_handler, ios);
    }
    if (r < 0) {
        _magenta_handle_close(h0);
        _magenta_handle_close(h1);
        free(ios);
        return r;
    }
    return h1;
}

static int vfs_watchdog(void* arg) {
    int txn = vfs_txn;
    for (;;) {
        _magenta_nanosleep(500000000ULL);
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
    if (mxio_dispatcher_create(&vfs_dispatcher, mxio_rio_handler) == NO_ERROR) {
        mxio_dispatcher_start(vfs_dispatcher);
    }
    mxr_thread_t* t;
    mxr_thread_create(vfs_watchdog, NULL, "vfs-watchdog", &t);
}
