// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devhost.h"
#include "device-internal.h"

#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <sync/completion.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <ddk/iotxn.h>
#include <zircon/device/device.h>
#include <zircon/device/vfs.h>

#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <fdio/debug.h>
#include <fdio/io.h>
#include <fdio/vfs.h>

#define MXDEBUG 0

#define CAN_WRITE(ios) (ios->flags & ZX_FS_RIGHT_WRITABLE)
#define CAN_READ(ios) (ios->flags & ZX_FS_RIGHT_READABLE)

devhost_iostate_t* create_devhost_iostate(zx_device_t* dev) {
    devhost_iostate_t* ios;
    if ((ios = calloc(1, sizeof(devhost_iostate_t))) == NULL) {
        return NULL;
    }
    ios->dev = dev;
    return ios;
}

static zx_status_t devhost_get_handles(zx_handle_t rh, zx_device_t* dev,
                                       const char* path, uint32_t flags) {
    zx_status_t r;
    zxrio_object_t obj;
    devhost_iostate_t* newios;

    if ((newios = create_devhost_iostate(dev)) == NULL) {
        zx_handle_close(rh);
        return ZX_ERR_NO_MEMORY;
    }

    // detect pipeline directive and discard all other
    // protocol flags
    bool pipeline = flags & ZX_FS_FLAG_PIPELINE;
    flags &= (~ZX_FS_FLAG_PIPELINE);

    newios->flags = flags;

    if ((r = device_open_at(dev, &dev, path, flags)) < 0) {
        printf("devhost_get_handles(%p:%s) open path='%s', r=%d\n",
               dev, dev->name, path ? path : "", r);
        if (pipeline) {
            goto fail_openat_pipelined;
        } else {
            goto fail_openat;
        }
    }
    newios->dev = dev;

    if (!pipeline) {
        if (dev->event > 0) {
            //TODO: read only?
            if ((r = zx_handle_duplicate(dev->event, ZX_RIGHT_SAME_RIGHTS, &obj.handle[0])) < 0) {
                goto fail_duplicate;
            }
            r = 1;
        } else {
            r = 0;
        }
        goto done;
fail_duplicate:
        device_close(dev, flags);
fail_openat:
        free(newios);
done:
        if (r < 0) {
            obj.status = r;
            obj.hcount = 0;
        } else {
            obj.status = ZX_OK;
            obj.type = FDIO_PROTOCOL_REMOTE;
            obj.hcount = r;
        }
        r = zx_channel_write(rh, 0, &obj, ZXRIO_OBJECT_MINSIZE,
                             obj.handle, obj.hcount);

        // Regardless of obj.status, if the zx_channel_write fails
        // we must close the handles that didn't get transmitted.
        if (r < 0) {
            for (size_t i = 0; i < obj.hcount; i++) {
                zx_handle_close(obj.handle[i]);
            }
        }

        // If we were reporting an error, we've already closed
        // the device and destroyed the iostate, so no matter
        // what we close the handle and return
        if (obj.status < 0) {
            zx_handle_close(rh);
            return obj.status;
        }

        // If we succeeded but the write failed, we have to
        // tear down because the channel is now dead
        if (r < 0) {
            goto fail;
        }
    }

    // Similarly, if we can't add the new ios and handle to the
    // dispatcher our only option is to give up and tear down.
    // In practice, this should never happen.
    if ((r = devhost_start_iostate(newios, rh)) < 0) {
        printf("devhost_get_handles: failed to start iostate\n");
        goto fail;
    }
    return ZX_OK;

fail:
    device_close(dev, flags);
fail_openat_pipelined:
    free(newios);
    zx_handle_close(rh);
    return r;
}

static void sync_io_complete(iotxn_t* txn, void* cookie) {
    completion_signal((completion_t*)cookie);
}

static ssize_t do_sync_io(zx_device_t* dev, uint32_t opcode, void* buf, size_t count, zx_off_t off) {
    if (dev->ops->iotxn_queue == NULL) {
        size_t actual;
        zx_status_t r;
        if (opcode == IOTXN_OP_READ) {
            r = dev_op_read(dev, buf, count, off, &actual);
        } else {
            r = dev_op_write(dev, buf, count, off, &actual);
        }
        if (r < 0) {
            return r;
        } else {
            return actual;
        }
    }
    iotxn_t* txn;
    zx_status_t status = iotxn_alloc(&txn, IOTXN_ALLOC_CONTIGUOUS | IOTXN_ALLOC_POOL, FDIO_CHUNK_SIZE);
    if (status != ZX_OK) {
        return status;
    }

    assert(count <= FDIO_CHUNK_SIZE);

    completion_t completion = COMPLETION_INIT;

    txn->opcode = opcode;
    txn->offset = off;
    txn->length = count;
    txn->complete_cb = sync_io_complete;
    txn->cookie = &completion;

    // if write, write the data to the iotxn
    if (opcode == IOTXN_OP_WRITE) {
        iotxn_copyto(txn, buf, txn->length, 0);
    }

    iotxn_queue(dev, txn);
    completion_wait(&completion, ZX_TIME_INFINITE);

    if (txn->status != ZX_OK) {
        size_t txn_status = txn->status;
        iotxn_release(txn);
        return txn_status;
    }

    // if read, get the data
    if (opcode == IOTXN_OP_READ) {
        iotxn_copyfrom(txn, buf, txn->actual, 0);
    }

    ssize_t actual = txn->actual;
    iotxn_release(txn);
    return actual;
}

static ssize_t do_ioctl(zx_device_t* dev, uint32_t op, const void* in_buf, size_t in_len, void* out_buf, size_t out_len) {
    zx_status_t r;
    switch (op) {
    case IOCTL_DEVICE_BIND: {
        char* drv_libname = in_len > 0 ? (char*)in_buf : NULL;
        if (in_len > PATH_MAX) {
            return ZX_ERR_BAD_PATH;
        }
        drv_libname[in_len] = 0;
        return device_bind(dev, drv_libname);
    }
    case IOCTL_DEVICE_GET_EVENT_HANDLE: {
        if (out_len < sizeof(zx_handle_t)) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        zx_handle_t* event = out_buf;
        r = zx_handle_duplicate(dev->event, ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ, event);
        if (r == ZX_OK) {
            r = sizeof(zx_handle_t);
        }
        return r;
    }
    case IOCTL_DEVICE_GET_DRIVER_NAME: {
        if (!dev->driver) {
            return ZX_ERR_NOT_SUPPORTED;
        }
        const char* name = dev->driver->name;
        if (name == NULL) {
            name = "unknown";
        }
        r = strlen(name);
        if (out_len < (size_t)r) {
            r = ZX_ERR_BUFFER_TOO_SMALL;
        } else {
            strncpy(out_buf, name, r);
        }
        return r;
    }
    case IOCTL_DEVICE_GET_DEVICE_NAME: {
        r = strlen(dev->name);
        if (out_len < (size_t)r) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        strncpy(out_buf, dev->name, r);
        return r;
    }
    case IOCTL_DEVICE_GET_TOPO_PATH: {
        size_t actual;
        if ((r = devhost_get_topo_path(dev, out_buf, out_len, &actual)) < 0) {
            return r;
        }
        return actual;
    }
    case IOCTL_DEVICE_DEBUG_SUSPEND: {
        return dev_op_suspend(dev, 0);
    }
    case IOCTL_DEVICE_DEBUG_RESUME: {
        return dev_op_resume(dev, 0);
    }
    case IOCTL_VFS_QUERY_FS: {
        const char* devhost_name = "devfs:host";
        if (out_len < sizeof(vfs_query_info_t) + strlen(devhost_name)) {
            return ZX_ERR_INVALID_ARGS;
        }
        vfs_query_info_t* info = (vfs_query_info_t*) out_buf;
        memset(info, 0, sizeof(*info));
        memcpy(info->name, devhost_name, strlen(devhost_name));
        return sizeof(vfs_query_info_t) + strlen(devhost_name);
    }
    default: {
        size_t actual = 0;
        r = dev_op_ioctl(dev, op, in_buf, in_len, out_buf, out_len, &actual);
        if (r == ZX_OK) {
            r = actual;
        }
    }
    }
    return r;
}

zx_status_t devhost_rio_handler(zxrio_msg_t* msg, void* cookie) {
    devhost_iostate_t* ios = cookie;
    zx_device_t* dev = ios->dev;
    uint32_t len = msg->datalen;
    int32_t arg = msg->arg;
    msg->datalen = 0;

    // ensure handle count specified by opcode matches reality
    if (msg->hcount != ZXRIO_HC(msg->op)) {
        return ZX_ERR_IO;
    }
    msg->hcount = 0;

    switch (ZXRIO_OP(msg->op)) {
    case ZXRIO_CLOSE:
        device_close(dev, ios->flags);
        // The ios released its reference to this device by calling device_close()
        // Put an invalid pointer in its dev field to ensure any use-after-release
        // attempts explode.
        ios->dev = (void*) 0xdead;
        return ZX_OK;
    case ZXRIO_OPEN:
        if ((len < 1) || (len > 1024)) {
            zx_handle_close(msg->handle[0]);
            return ERR_DISPATCHER_INDIRECT;
        }
        msg->data[len] = 0;
        // fallthrough
    case ZXRIO_CLONE: {
        char* path = NULL;
        uint32_t flags = arg;
        if (ZXRIO_OP(msg->op) == ZXRIO_OPEN) {
            xprintf("devhost_rio_handler() open dev %p name '%s' at '%s'\n",
                    dev, dev->name, (char*) msg->data);
            if (strcmp((char*)msg->data, ".")) {
                path = (char*) msg->data;
            }
        } else {
            xprintf("devhost_rio_handler() clone dev %p name '%s'\n", dev, dev->name);
            flags = ios->flags | (flags & ZX_FS_FLAG_PIPELINE);
        }
        devhost_get_handles(msg->handle[0], dev, path, flags);
        return ERR_DISPATCHER_INDIRECT;
    }
    case ZXRIO_READ: {
        if (!CAN_READ(ios)) {
            return ZX_ERR_ACCESS_DENIED;
        }
        zx_status_t r = do_sync_io(dev, IOTXN_OP_READ, msg->data, arg, ios->io_off);
        if (r >= 0) {
            ios->io_off += r;
            msg->arg2.off = ios->io_off;
            msg->datalen = r;
        }
        return r;
    }
    case ZXRIO_READ_AT: {
        if (!CAN_READ(ios)) {
            return ZX_ERR_ACCESS_DENIED;
        }
        zx_status_t r = do_sync_io(dev, IOTXN_OP_READ, msg->data, arg, msg->arg2.off);
        if (r >= 0) {
            msg->datalen = r;
        }
        return r;
    }
    case ZXRIO_WRITE: {
        if (!CAN_WRITE(ios)) {
            return ZX_ERR_ACCESS_DENIED;
        }
        zx_status_t r = do_sync_io(dev, IOTXN_OP_WRITE, msg->data, len, ios->io_off);
        if (r >= 0) {
            ios->io_off += r;
            msg->arg2.off = ios->io_off;
        }
        return r;
    }
    case ZXRIO_WRITE_AT: {
        if (!CAN_WRITE(ios)) {
            return ZX_ERR_ACCESS_DENIED;
        }
        zx_status_t r = do_sync_io(dev, IOTXN_OP_WRITE, msg->data, len, msg->arg2.off);
        return r;
    }
    case ZXRIO_SEEK: {
        size_t end, n;
        end = dev_op_get_size(dev);
        switch (arg) {
        case SEEK_SET:
            if ((msg->arg2.off < 0) || ((size_t)msg->arg2.off > end)) {
                return ZX_ERR_INVALID_ARGS;
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
                    return ZX_ERR_INVALID_ARGS;
                }
            } else {
                // positive seek
                if (n < ios->io_off) {
                    // wrapped around
                    return ZX_ERR_INVALID_ARGS;
                }
            }
            break;
        case SEEK_END:
            n = end + msg->arg2.off;
            if (msg->arg2.off <= 0) {
                // if negative or exact-end seek
                if (n > end) {
                    // wrapped around
                    return ZX_ERR_INVALID_ARGS;
                }
            } else {
                if (n < end) {
                    // wrapped around
                    return ZX_ERR_INVALID_ARGS;
                }
            }
            break;
        default:
            return ZX_ERR_INVALID_ARGS;
        }
        if (n > end) {
            // devices may not seek past the end
            return ZX_ERR_INVALID_ARGS;
        }
        ios->io_off = n;
        msg->arg2.off = ios->io_off;
        return ZX_OK;
    }
    case ZXRIO_STAT: {
        msg->datalen = sizeof(vnattr_t);
        vnattr_t* attr = (void*)msg->data;
        memset(attr, 0, sizeof(vnattr_t));
        attr->mode = V_TYPE_CDEV | V_IRUSR | V_IWUSR;
        attr->size = dev_op_get_size(dev);
        attr->nlink = 1;
        return msg->datalen;
    }
    case ZXRIO_SYNC: {
        return do_ioctl(dev, IOCTL_DEVICE_SYNC, NULL, 0, NULL, 0);
    }
    case ZXRIO_IOCTL_1H: {
        if ((len > FDIO_IOCTL_MAX_INPUT) ||
            (arg > (ssize_t)sizeof(msg->data)) ||
            (IOCTL_KIND(msg->arg2.op) != IOCTL_KIND_SET_HANDLE)) {
            zx_handle_close(msg->handle[0]);
            return ZX_ERR_INVALID_ARGS;
        }
        if (len < sizeof(zx_handle_t)) {
            len = sizeof(zx_handle_t);
        }

        char in_buf[FDIO_IOCTL_MAX_INPUT];
        // The sending side copied the handle into msg->handle[0]
        // so that it would be sent via channel_write().  Here we
        // copy the local version back into the space in the buffer
        // that the original occupied.
        memcpy(in_buf, msg->handle, sizeof(zx_handle_t));
        memcpy(in_buf + sizeof(zx_handle_t), msg->data + sizeof(zx_handle_t),
               len - sizeof(zx_handle_t));

        zx_status_t r = do_ioctl(dev, msg->arg2.op, in_buf, len, msg->data, arg);

        if (r == ZX_ERR_NOT_SUPPORTED) {
            zx_handle_close(msg->handle[0]);
        } else if (r >= 0) {
            msg->datalen = r;
        }
        return r;
    }
    case ZXRIO_IOCTL: {
        if ((len > FDIO_IOCTL_MAX_INPUT) ||
            (arg > (ssize_t)sizeof(msg->data)) ||
            (IOCTL_KIND(msg->arg2.op) == IOCTL_KIND_SET_HANDLE)) {
            return ZX_ERR_INVALID_ARGS;
        }

        char in_buf[FDIO_IOCTL_MAX_INPUT];
        memcpy(in_buf, msg->data, len);

        zx_status_t r = do_ioctl(dev, msg->arg2.op, in_buf, len, msg->data, arg);
        if (r >= 0) {
            switch (IOCTL_KIND(msg->arg2.op)) {
            case IOCTL_KIND_GET_HANDLE:
                msg->hcount = 1;
                memcpy(msg->handle, msg->data, sizeof(zx_handle_t));
                break;
            case IOCTL_KIND_GET_TWO_HANDLES:
                msg->hcount = 2;
                memcpy(msg->handle, msg->data, 2 * sizeof(zx_handle_t));
                break;
            case IOCTL_KIND_GET_THREE_HANDLES:
                msg->hcount = 3;
                memcpy(msg->handle, msg->data, 3 * sizeof(zx_handle_t));
                break;
            }
            msg->datalen = r;
            msg->arg2.off = ios->io_off;
        }
        return r;
    }
    default:
        // close inbound handles so they do not leak
        for (unsigned i = 0; i < ZXRIO_HC(msg->op); i++) {
            zx_handle_close(msg->handle[i]);
        }
        return ZX_ERR_NOT_SUPPORTED;
    }
}
