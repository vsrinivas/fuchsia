// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vfs.h"
#include "dnode.h"
#include "devmgr.h"

#include <mxio/debug.h>
#include <mxio/dispatcher.h>
#include <mxio/io.h>
#include <mxio/remoteio.h>

#include <ddk/device.h>

#include <system/listnode.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>

#include <runtime/thread.h>
#include <runtime/mutex.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MXDEBUG 0

#define DEBUG_TRACK_NAMES 1

mxr_mutex_t vfs_lock = MXR_MUTEX_INIT;

static list_node_t iostate_list = LIST_INITIAL_VALUE(iostate_list);
static mxr_mutex_t iostate_lock = MXR_MUTEX_INIT;

void track_iostate(iostate_t* ios, const char* fn) {
#if DEBUG_TRACK_NAMES
    if (fn) {
        ios->fn = strdup(fn);
    }
#endif
    mxr_mutex_lock(&iostate_lock);
    list_add_tail(&iostate_list, &ios->node);
    mxr_mutex_unlock(&iostate_lock);
}

void untrack_iostate(iostate_t* ios) {
    mxr_mutex_lock(&iostate_lock);
    list_delete(&ios->node);
    mxr_mutex_unlock(&iostate_lock);
#if DEBUG_TRACK_NAMES
    free((void*) ios->fn);
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
            printf("vfs_walk: vn=%p name='%s' (remote)\n", vn, path);
            *out = vn;
            *pathout = path;
#if WITH_REPLY_PIPE
            if (vn->vfs && vn->vfs->remote) {
                return vn->vfs->remote;
            }
#endif
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
    xprintf("vfs_open: path='%s' flags=%d\n", path, flags);
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
#if WITH_REPLY_PIPE
        if (vn->vfs && vn->vfs->remote) {
            *pathout = ".";
            return vn->vfs->remote;
        }
#endif
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
        return ERR_TOO_BIG;
    de->size = sz;
    de->type = type;
    memcpy(de->name, name, len);
    de->name[len] = 0;
    return sz;
}

static mx_status_t vfs_get_handles(vnode_t* vn, bool as_dir, mx_handle_t* hnds, uint32_t* ids, const char* trackfn) {
    mx_status_t r;
    if (vn->flags & V_FLAG_DEVICE && !as_dir) {
        // opening a device, get devmgr handles
        r = devmgr_get_handles((mx_device_t*)vn->pdata, hnds, ids);
    } else {
        // local vnode or device as a directory, we will create the handles
        hnds[0] = vfs_create_handle(vn, trackfn);
        ids[0] = MX_HND_TYPE_MXIO_REMOTE;
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

static vnode_t* volatile vfs_txn_vn;
static volatile int vfs_txn_op;

static mx_status_t _vfs_open(mxrio_msg_t* msg, mx_handle_t rh,
                             vnode_t* vn, const char* path,
                             uint32_t flags, uint32_t mode) {
    mx_status_t r;
    mxr_mutex_lock(&vfs_lock);
    r = vfs_open(vn, &vn, path, &path, flags, mode);
    mxr_mutex_unlock(&vfs_lock);
    if (r < 0) {
        xprintf("vfs: open: r=%d\n", r);
        return r;
    }
#if WITH_REPLY_PIPE
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
#endif
    uint32_t ids[VFS_MAX_HANDLES];
    if ((r = vfs_get_handles(vn, flags & O_DIRECTORY, msg->handle, ids, (const char*)msg->data)) < 0) {
        vn->ops->close(vn);
        return r;
    }
#if WITH_REPLY_PIPE
    if (ids[0] == 0) {
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
#endif
    // drop the ref from open or create
    // the backend behind get_handles holds the on-going ref
    vn_release(vn);

    // TODO: ensure this is always true:
    msg->arg2.protocol = MXIO_PROTOCOL_REMOTE;
    msg->hcount = r;
    xprintf("vfs: open: h=%x\n", msg->handle[0]);
    return NO_ERROR;
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
        char* path = (char*) msg->data;
        if ((len < 1) || (len > 1024)) {
            return ERR_INVALID_ARGS;
        }
        path[len] = 0;
        xprintf("vfs: open name='%s' flags=%d mode=%lld\n", path, arg, msg->arg2.mode);
        return _vfs_open(msg, rh, vn, path, arg, msg->arg2.mode);
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
    case MXRIO_WRITE: {
        ssize_t r = vn->ops->write(vn, msg->data, len, ios->io_off);
        if (r >= 0) {
            ios->io_off += r;
            msg->arg2.off = ios->io_off;
        }
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
        mxr_mutex_lock(&vfs_lock);
        r = vn->ops->readdir(vn, &ios->dircookie, msg->data, arg);
        mxr_mutex_unlock(&vfs_lock);
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

        ssize_t r = vn->ops->ioctl(vn, msg->arg2.op, in_buf, len, msg->data, arg);
        if (r >= 0) {
            msg->arg2.off = 0;
            msg->datalen = r;
        }
        return r;
    }
    case MXRIO_UNLINK:
        return vn->ops->unlink(vn, (const char*) msg->data, len);
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

    if ((r = mx_message_pipe_create(h, 0)) < 0) {
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
        mxio_dispatcher_start(vfs_dispatcher);
    }
    mxr_thread_t* t;
    mxr_thread_create(vfs_watchdog, NULL, "vfs-watchdog", &t);
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
    mxr_mutex_lock(&iostate_lock);
    list_for_every_entry(&iostate_list, ios, iostate_t, node) {
        printf("obj %p '%s'\n", ios->vn, ios->fn ? ios->fn : "???");
    }
    mxr_mutex_unlock(&iostate_lock);
}
