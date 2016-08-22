// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devmgr.h"
#include "vfs.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>

#include <ddk/completion.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/iotxn.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <magenta/processargs.h>

#include <mxio/debug.h>
#include <mxio/dispatcher.h>
#include <mxio/remoteio.h>
#include <mxio/vfs.h>

#include <system/listnode.h>

#define MXDEBUG 0

static const char* name = "devmgr";

iostate_t* create_iostate(mx_device_t* dev) {
    iostate_t* ios;
    if ((ios = calloc(1, sizeof(iostate_t))) == NULL) {
        return NULL;
    }
    ios->dev = dev;
    return ios;
}

mx_status_t __mxrio_clone(mx_handle_t h, mx_handle_t* handles, uint32_t* types);

#if !WITH_REPLY_PIPE
static mtx_t rio_lock = MTX_INIT;
#endif

// This is called from both the vfs handler thread and console start thread
// and if not protected by rio_lock, they can step on each other when cloning
// remoted devices.
//
// TODO: eventually this should be integrated with core devmgr locking, but
//       that will require a bit more work.  This resolves the immediate issue.
mx_status_t devmgr_get_handles(mx_device_t* dev, mx_handle_t* handles, uint32_t* ids) {
    mx_status_t r;
    iostate_t* newios;
    if (devmgr_is_remote) {
        name = "devhost";
    }

    // remote device: clone from remote devhost
    // TODO: timeout or handoff
    if (dev->flags & DEV_FLAG_REMOTE) {
#if WITH_REPLY_PIPE
        // notify caller that their OPEN or CLONE
        // must be routed to a different server
        handles[0] = dev->remote;
        ids[0] = 0;
        return 1;
#else
        mtx_lock(&rio_lock);
        r = __mxrio_clone(dev->remote, handles, ids);
        mtx_unlock(&rio_lock);
        return r;
#endif
    }

    if ((newios = create_iostate(dev)) == NULL) {
        return ERR_NO_MEMORY;
    }

    mx_handle_t h[2];
    if ((r = mx_message_pipe_create(h, 0)) < 0) {
        free(newios);
        return r;
    }
    handles[0] = h[0];
    ids[0] = MX_HND_TYPE_MXIO_REMOTE;

    if ((r = device_open(dev, &dev, 0)) < 0) {
        printf("%s_get_handles(%p) open %d\n", name, dev, r);
        goto fail1;
    }
    newios->dev = dev;

    if (dev->event > 0) {
        //TODO: read only?
        if ((handles[1] = mx_handle_duplicate(dev->event, MX_RIGHT_SAME_RIGHTS)) < 0) {
            r = handles[1];
            goto fail2;
        }
        ids[1] = MX_HND_TYPE_MXIO_REMOTE;
        r = 2;
    } else {
        r = 1;
    }

    char tmp[MX_DEVICE_NAME_MAX + 9];
    snprintf(tmp, sizeof(tmp), "device:%s", dev->name);
    track_iostate(newios, tmp);

    mxio_dispatcher_add(devmgr_rio_dispatcher, h[1], devmgr_rio_handler, newios);
    return r;

fail2:
    device_close(dev);
fail1:
    mx_handle_close(h[0]);
    mx_handle_close(h[1]);
    free(newios);
    return r;
}

mx_status_t txn_handoff_clone(mx_handle_t srv, mx_handle_t rh) {
    mxrio_msg_t msg;
    memset(&msg, 0, MXRIO_HDR_SZ);
    msg.op = MXRIO_CLONE;
    return mxrio_txn_handoff(srv, rh, &msg);
}

#define TXN_SIZE 0x2000 // max size of rio is 8k

static void sync_io_complete(iotxn_t* txn, void* cookie) {
    completion_signal((completion_t*)cookie);
}

static ssize_t do_sync_io(mx_device_t* dev, uint32_t opcode, void* buf, size_t count, mx_off_t off) {
    iotxn_t* txn;
    mx_status_t status = iotxn_alloc(&txn, 0, TXN_SIZE, 0);
    if (status != NO_ERROR) {
        return status;
    }

    assert(count <= TXN_SIZE);

    completion_t completion = COMPLETION_INIT;

    txn->opcode = opcode;
    txn->offset = off;
    txn->length = count;
    txn->complete_cb = sync_io_complete;
    txn->cookie = &completion;

    // if write, write the data to the iotxn
    if (opcode == IOTXN_OP_WRITE) {
        txn->ops->copyto(txn, buf, txn->length, 0);
    }

    dev->ops->iotxn_queue(dev, txn);
    completion_wait(&completion, MX_TIME_INFINITE);

    if (txn->status != NO_ERROR) {
        txn->ops->release(txn);
        return txn->status;
    }

    // if read, get the data
    if (opcode == IOTXN_OP_READ) {
        txn->ops->copyfrom(txn, buf, txn->actual, 0);
    }

    ssize_t actual = txn->actual;
    txn->ops->release(txn);
    return actual;
}

mx_status_t devmgr_rio_handler(mxrio_msg_t* msg, mx_handle_t rh, void* cookie) {
    iostate_t* ios = cookie;
    mx_device_t* dev = ios->dev;
    uint32_t len = msg->datalen;
    int32_t arg = msg->arg;
    msg->datalen = 0;

    for (unsigned i = 0; i < msg->hcount; i++) {
        mx_handle_close(msg->handle[i]);
    }

    switch (MXRIO_OP(msg->op)) {
    case MXRIO_CLOSE:
        device_close(dev);
        untrack_iostate(ios);
        free(ios);
        return NO_ERROR;
    case MXRIO_CLONE: {
        xprintf("%s_rio_handler() clone dev %p name '%s'\n", name, dev, dev->name);
        uint32_t ids[VFS_MAX_HANDLES];
        mx_status_t r = devmgr_get_handles(dev, msg->handle, ids);
        if (r < 0) {
            return r;
        }
#if WITH_REPLY_PIPE
        if (ids[0] == 0) {
            // device is non-local, handle is the server that
            // can clone it for us, redirect the rpc to there
            if ((r = txn_handoff_clone(msg->handle[0], rh)) < 0) {
                printf("txn_handoff_clone() failed %d\n", r);
                return r;
            }
            return ERR_DISPATCHER_INDIRECT;
        }
#endif
        msg->arg2.protocol = MXIO_PROTOCOL_REMOTE;
        msg->hcount = r;
        return NO_ERROR;
    }
    case MXRIO_READ: {
        mx_status_t r = do_sync_io(dev, IOTXN_OP_READ, msg->data, arg, ios->io_off);
        if (r >= 0) {
            ios->io_off += r;
            msg->arg2.off = ios->io_off;
            msg->datalen = r;
        }
        return r;
    }
    case MXRIO_WRITE: {
        mx_status_t r = do_sync_io(dev, IOTXN_OP_WRITE, msg->data, len, ios->io_off);
        if (r >= 0) {
            ios->io_off += r;
            msg->arg2.off = ios->io_off;
        }
        return r;
    }
    case MXRIO_SEEK: {
        size_t end, n;
        end = dev->ops->get_size(dev);
        switch (arg) {
        case SEEK_SET:
            if ((msg->arg2.off < 0) || ((size_t)msg->arg2.off > end)) {
                return ERR_INVALID_ARGS;
            }
            n = msg->arg2.off;
            break;
        case SEEK_CUR:
            // TODO: track seekability with flag, don't update off
            // at all on read/write if not seekable
            n = ios->io_off + msg->arg2.off;
            if (msg->arg2.off < 0) {
                // if negative seek
                if (n > ios->io_off) {
                    // wrapped around
                    return ERR_INVALID_ARGS;
                }
            } else {
                // positive seek
                if (n < ios->io_off) {
                    // wrapped around
                    return ERR_INVALID_ARGS;
                }
            }
            break;
        case SEEK_END:
            n = end + msg->arg2.off;
            if (msg->arg2.off <= 0) {
                // if negative or exact-end seek
                if (n > end) {
                    // wrapped around
                    return ERR_INVALID_ARGS;
                }
            } else {
                if (n < end) {
                    // wrapped around
                    return ERR_INVALID_ARGS;
                }
            }
            break;
        default:
            return ERR_INVALID_ARGS;
        }
        if (n > end) {
            // devices may not seek past the end
            return ERR_INVALID_ARGS;
        }
        ios->io_off = n;
        msg->arg2.off = ios->io_off;
        return NO_ERROR;
    }
    case MXRIO_STAT: {
        msg->datalen = sizeof(vnattr_t);
        vnattr_t* attr = (void*) msg->data;
        memset(attr, 0, sizeof(vnattr_t));
        attr->mode = V_TYPE_CDEV | V_IRUSR | V_IWUSR;
        attr->size = dev->ops->get_size(dev);
        return msg->datalen;
    }
    case MXRIO_IOCTL: {
        if (len > MXIO_IOCTL_MAX_INPUT || arg > (ssize_t)sizeof(msg->data)) {
            return ERR_INVALID_ARGS;
        }
        char in_buf[MXIO_IOCTL_MAX_INPUT];
        memcpy(in_buf, msg->data, len);
        mx_status_t r = dev->ops->ioctl(dev, msg->arg2.op, in_buf, len, msg->data, arg);
        if (r >= 0) {
            if (msg->arg2.op == IOCTL_DEVICE_GET_HANDLE) {
                msg->hcount = 1;
                memcpy(msg->handle, msg->data, sizeof(mx_handle_t));
            }
            msg->datalen = r;
            msg->arg2.off = ios->io_off;
        }
        return r;
    }
    default:
        return ERR_NOT_SUPPORTED;
    }
}

