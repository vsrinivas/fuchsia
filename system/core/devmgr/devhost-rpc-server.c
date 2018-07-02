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

#include <zircon/device/device.h>
#include <zircon/device/vfs.h>

#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <lib/fdio/debug.h>
#include <lib/fdio/io.fidl.h>
#include <lib/fdio/io.h>
#include <lib/fdio/vfs.h>

#define ZXDEBUG 0

#define CAN_WRITE(ios) (ios->flags & ZX_FS_RIGHT_WRITABLE)
#define CAN_READ(ios) (ios->flags & ZX_FS_RIGHT_READABLE)

void describe_error(zx_handle_t h, zx_status_t status) {
    zxrio_describe_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.op = ZXRIO_ON_OPEN;
    msg.status = status;
    zx_channel_write(h, 0, &msg, sizeof(msg), NULL, 0);
    zx_handle_close(h);
}

static zx_status_t create_description(zx_device_t* dev, zxrio_describe_t* msg,
                                      zx_handle_t* handle) {
    memset(msg, 0, sizeof(*msg));
    msg->op = ZXRIO_ON_OPEN;
    msg->extra.tag = FDIO_PROTOCOL_DEVICE;
    msg->status = ZX_OK;
    msg->extra_ptr = (zxrio_object_info_t*)FIDL_ALLOC_PRESENT;
    *handle = ZX_HANDLE_INVALID;
    if (dev->event != ZX_HANDLE_INVALID) {
        //TODO: read only?
        zx_status_t r;
        if ((r = zx_handle_duplicate(dev->event, ZX_RIGHT_SAME_RIGHTS,
                                     handle)) != ZX_OK) {
            msg->status = r;
            return r;
        }
        msg->extra.device.e = FIDL_HANDLE_PRESENT;
    } else {
        msg->extra.device.e = FIDL_HANDLE_ABSENT;
    }

    return ZX_OK;
}

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
    devhost_iostate_t* newios;
    // detect response directives and discard all other
    // protocol flags
    bool describe = flags & ZX_FS_FLAG_DESCRIBE;
    flags &= (~ZX_FS_FLAG_DESCRIBE);

    if ((newios = create_devhost_iostate(dev)) == NULL) {
        if (describe) {
            describe_error(rh, ZX_ERR_NO_MEMORY);
        }
        return ZX_ERR_NO_MEMORY;
    }

    newios->flags = flags;

    if ((r = device_open_at(dev, &dev, path, flags)) < 0) {
        fprintf(stderr, "devhost_get_handles(%p:%s) open path='%s', r=%d\n",
                dev, dev->name, path ? path : "", r);
        goto fail;
    }
    newios->dev = dev;

    if (describe) {
        zxrio_describe_t info;
        zx_handle_t handle;
        if ((r = create_description(dev, &info, &handle)) != ZX_OK) {
            goto fail_open;
        }
        uint32_t hcount = (handle != ZX_HANDLE_INVALID) ? 1 : 0;
        r = zx_channel_write(rh, 0, &info, sizeof(info), &handle, hcount);
        if (r != ZX_OK) {
            goto fail_open;
        }
    }

    // If we can't add the new ios and handle to the dispatcher our only option
    // is to give up and tear down.  In practice, this should never happen.
    if ((r = devhost_start_iostate(newios, rh)) < 0) {
        fprintf(stderr, "devhost_get_handles: failed to start iostate\n");
        goto fail;
    }
    return ZX_OK;

fail_open:
    device_close(dev, flags);
fail:
    free(newios);
    if (describe) {
        describe_error(rh, r);
    } else {
        zx_handle_close(rh);
    }
    return r;
}

#define DO_READ 0
#define DO_WRITE 1

static ssize_t do_sync_io(zx_device_t* dev, uint32_t opcode, void* buf, size_t count, zx_off_t off) {
    size_t actual;
    zx_status_t r;
    if (opcode == DO_READ) {
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
        r = zx_handle_duplicate(dev->event, ZX_RIGHTS_BASIC | ZX_RIGHT_READ, event);
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
        size_t actual = strlen(dev->name) + 1;
        if (out_len < actual) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        memcpy(out_buf, dev->name, actual);
        return actual;
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
    case IOCTL_DEVICE_GET_DRIVER_LOG_FLAGS: {
        if (!dev->driver) {
            return ZX_ERR_UNAVAILABLE;
        }
        if (out_len < sizeof(uint32_t)) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        *((uint32_t *)out_buf) = dev->driver->driver_rec->log_flags;
        return sizeof(uint32_t);
    }
    case IOCTL_DEVICE_SET_DRIVER_LOG_FLAGS: {
        if (!dev->driver) {
            return ZX_ERR_UNAVAILABLE;
        }
        if (in_len < sizeof(driver_log_flags_t)) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        driver_log_flags_t* flags = (driver_log_flags_t *)in_buf;
        dev->driver->driver_rec->log_flags &= ~flags->clear;
        dev->driver->driver_rec->log_flags |= flags->set;
        return sizeof(driver_log_flags_t);
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

static void discard_handles(zx_handle_t* handles, size_t count) {
    while (count-- > 0) {
        zx_handle_close(*handles++);
    }
}

zx_status_t devhost_rio_handler(zxrio_msg_t* msg, void* cookie) {
    devhost_iostate_t* ios = cookie;
    zx_device_t* dev = ios->dev;
    uint32_t len = msg->datalen;
    int32_t arg = msg->arg;

    if (!ZXRIO_FIDL_MSG(msg->op)) {
        msg->datalen = 0;
        msg->hcount = 0;
    }

    switch (ZXRIO_OP(msg->op)) {
    case ZXFIDL_CLOSE:
    case ZXRIO_CLOSE:
        device_close(dev, ios->flags);
        // The ios released its reference to this device by calling device_close()
        // Put an invalid pointer in its dev field to ensure any use-after-release
        // attempts explode.
        ios->dev = (void*) 0xdead;
        return ZX_OK;
    case ZXFIDL_OPEN:
    case ZXRIO_OPEN: {
        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        fuchsia_io_DirectoryOpenRequest* request = (fuchsia_io_DirectoryOpenRequest*) msg;
        zx_handle_t h;
        char* name;
        uint32_t flags;

        if (fidl) {
            len = request->path.size;
            h = request->object;
            name = request->path.data;
            flags = request->flags;
        } else {
            h = msg->handle[0];
            name = (char*) msg->data;
            flags = arg;
        }

        if ((len < 1) || (len > 1024)) {
            zx_handle_close(h);
            return ERR_DISPATCHER_INDIRECT;
        }
        name[len] = 0;
        if (!strcmp(name, ".")) {
            name = NULL;
        }
        devhost_get_handles(h, dev, name, flags);
        return ERR_DISPATCHER_INDIRECT;
    }
    case ZXFIDL_CLONE:
    case ZXRIO_CLONE: {
        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        fuchsia_io_ObjectCloneRequest* request = (fuchsia_io_ObjectCloneRequest*) msg;
        zx_handle_t h;
        uint32_t flags;

        if (fidl) {
            h = request->object;
            flags = request->flags;
        } else {
            h = msg->handle[0];
            flags = arg;
        }

        flags = ios->flags | (flags & ZX_FS_FLAG_DESCRIBE);
        devhost_get_handles(h, dev, NULL, flags);
        return ERR_DISPATCHER_INDIRECT;
    }
    case ZXFIDL_READ:
    case ZXRIO_READ: {
        if (!CAN_READ(ios)) {
            return ZX_ERR_ACCESS_DENIED;
        }
        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        fuchsia_io_FileReadRequest* request = (fuchsia_io_FileReadRequest*) msg;
        fuchsia_io_FileReadResponse* response = (fuchsia_io_FileReadResponse*) msg;
        void* data;
        if (fidl) {
            data = (void*)((uintptr_t)(response) +
                    FIDL_ALIGN(sizeof(fuchsia_io_FileReadResponse)));
            len = request->count;
        } else {
            data = msg->data;
            len = arg;
        }

        zx_status_t r = do_sync_io(dev, DO_READ, data, len, ios->io_off);
        if (r >= 0) {
            ios->io_off += r;
            if (fidl) {
                response->data.count = r;
                r = ZX_OK;
            } else {
                msg->datalen = r;
            }
        }
        return r;
    }
    case ZXFIDL_READ_AT:
    case ZXRIO_READ_AT: {
        if (!CAN_READ(ios)) {
            return ZX_ERR_ACCESS_DENIED;
        }
        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        fuchsia_io_FileReadAtRequest* request = (fuchsia_io_FileReadAtRequest*) msg;
        fuchsia_io_FileReadAtResponse* response = (fuchsia_io_FileReadAtResponse*) msg;
        void* data;
        uint64_t offset;
        if (fidl) {
            data = (void*)((uintptr_t)(response) +
                    FIDL_ALIGN(sizeof(fuchsia_io_FileReadAtResponse)));
            len = request->count;
            offset = request->offset;
        } else {
            data = msg->data;
            len = arg;
            offset = msg->arg2.off;
        }
        zx_status_t r = do_sync_io(dev, DO_READ, data, len, offset);

        if (fidl) {
            response->data.count = r;
            return r > 0 ? ZX_OK : r;
        } else {
            msg->datalen = r;
            return r;
        }
    }
    case ZXFIDL_WRITE:
    case ZXRIO_WRITE: {
        if (!CAN_WRITE(ios)) {
            return ZX_ERR_ACCESS_DENIED;
        }
        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        fuchsia_io_FileWriteRequest* request = (fuchsia_io_FileWriteRequest*) msg;
        fuchsia_io_FileWriteResponse* response = (fuchsia_io_FileWriteResponse*) msg;
        void* data;
        if (fidl) {
            data = request->data.data;
            len = request->data.count;
        } else {
            data = msg->data;
        }

        zx_status_t r = do_sync_io(dev, DO_WRITE, data, len, ios->io_off);
        if (r >= 0) {
            ios->io_off += r;
            if (fidl) {
                response->actual = r;
                r = ZX_OK;
            }
        }
        return r;
    }
    case ZXFIDL_WRITE_AT:
    case ZXRIO_WRITE_AT: {
        if (!CAN_WRITE(ios)) {
            return ZX_ERR_ACCESS_DENIED;
        }
        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        fuchsia_io_FileWriteAtRequest* request = (fuchsia_io_FileWriteAtRequest*) msg;
        fuchsia_io_FileWriteAtResponse* response = (fuchsia_io_FileWriteAtResponse*) msg;
        void* data;
        uint64_t offset;
        if (fidl) {
            data = request->data.data;
            len = request->data.count;
            offset = request->offset;
        } else {
            data = msg->data;
            offset = msg->arg2.off;
        }

        zx_status_t r = do_sync_io(dev, DO_WRITE, data, len, offset);

        if (fidl) {
            response->actual = r > 0 ? r : 0;
            return r > 0 ? ZX_OK : r;
        } else {
            return r;
        }
    }
    case ZXFIDL_SEEK:
    case ZXRIO_SEEK: {
        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        fuchsia_io_FileSeekRequest* request = (fuchsia_io_FileSeekRequest*) msg;
        fuchsia_io_FileSeekResponse* response = (fuchsia_io_FileSeekResponse*) msg;

        static_assert(SEEK_SET == fuchsia_io_SeekOrigin_Start, "");
        static_assert(SEEK_CUR == fuchsia_io_SeekOrigin_Current, "");
        static_assert(SEEK_END == fuchsia_io_SeekOrigin_End, "");

        off_t offset;
        int whence;
        if (fidl) {
            offset = request->offset;
            whence = request->start;
        } else {
            offset = msg->arg2.off;
            whence = arg;
        }

        size_t end, n;
        end = dev_op_get_size(dev);
        switch (whence) {
        case SEEK_SET:
            if ((offset < 0) || ((size_t)offset > end)) {
                return ZX_ERR_INVALID_ARGS;
            }
            n = offset;
            break;
        case SEEK_CUR:
            // TODO: track seekability with flag, don't update off
            // at all on read/write if not seekable
            n = ios->io_off + offset;
            if (offset < 0) {
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
            n = end + offset;
            if (offset <= 0) {
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
        if (fidl) {
            response->offset = ios->io_off;
        } else {
            msg->arg2.off = ios->io_off;
        }
        return ZX_OK;
    }
    case ZXFIDL_STAT:
    case ZXRIO_STAT: {
        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        fuchsia_io_NodeGetAttrResponse* response = (fuchsia_io_NodeGetAttrResponse*) msg;
        if (fidl) {
            memset(&response->attributes, 0, sizeof(response->attributes));
            response->attributes.mode = V_TYPE_CDEV | V_IRUSR | V_IWUSR;
            response->attributes.content_size = dev_op_get_size(dev);
            response->attributes.link_count = 1;
            return ZX_OK;
        }

        msg->datalen = sizeof(vnattr_t);
        vnattr_t* attr = (void*)msg->data;
        memset(attr, 0, sizeof(vnattr_t));
        attr->mode = V_TYPE_CDEV | V_IRUSR | V_IWUSR;
        attr->size = dev_op_get_size(dev);
        attr->nlink = 1;
        return msg->datalen;
    }
    case ZXFIDL_SYNC:
    case ZXRIO_SYNC: {
        return do_ioctl(dev, IOCTL_DEVICE_SYNC, NULL, 0, NULL, 0);
    }
    case ZXFIDL_IOCTL: {
        fuchsia_io_NodeIoctlRequest* request = (fuchsia_io_NodeIoctlRequest*) msg;
        fuchsia_io_NodeIoctlResponse* response = (fuchsia_io_NodeIoctlResponse*) msg;

        char in_buf[FDIO_IOCTL_MAX_INPUT];
        size_t hsize = request->handles.count * sizeof(zx_handle_t);
        if (hsize + request->in.count > FDIO_IOCTL_MAX_INPUT) {
            discard_handles(request->handles.data, request->handles.count);
            return ZX_ERR_INVALID_ARGS;
        }
        memcpy(in_buf, request->in.data, request->in.count);
        memcpy(in_buf, request->handles.data, hsize);

        uint32_t op = request->opcode;
        void* secondary = (void*)((uintptr_t)(msg) +
                FIDL_ALIGN(sizeof(fuchsia_io_NodeIoctlResponse)));
        zx_status_t r = do_ioctl(dev, op, in_buf, request->in.count,
                                 secondary, request->max_out);
        if (r >= 0) {
            response->out.count = r;
            r = ZX_OK;
            switch (IOCTL_KIND(op)) {
            case IOCTL_KIND_GET_HANDLE:
                response->handles.count = 1;
                break;
            case IOCTL_KIND_GET_TWO_HANDLES:
                response->handles.count = 2;
                break;
            case IOCTL_KIND_GET_THREE_HANDLES:
                response->handles.count = 3;
                break;
            default:
                response->handles.count = 0;
                break;
            }
        }
        response->handles.data = secondary;
        response->out.data = secondary;
        return r;
    }
    case ZXRIO_IOCTL_2H: {
        if ((len > FDIO_IOCTL_MAX_INPUT) ||
            (arg > (ssize_t)sizeof(msg->data)) ||
            (IOCTL_KIND(msg->arg2.op) != IOCTL_KIND_SET_TWO_HANDLES)) {
            zx_handle_close(msg->handle[0]);
            zx_handle_close(msg->handle[1]);
            return ZX_ERR_INVALID_ARGS;
        }
        size_t hsize = 2 * sizeof(zx_handle_t);
        if (len < hsize) {
            len = hsize;
        }

        char in_buf[FDIO_IOCTL_MAX_INPUT];
        memcpy(in_buf, msg->handle, hsize);
        memcpy(in_buf + hsize, msg->data + hsize, len - hsize);

        zx_status_t r = do_ioctl(dev, msg->arg2.op, in_buf, len, msg->data, arg);

        if (r == ZX_ERR_NOT_SUPPORTED) {
            zx_handle_close(msg->handle[0]);
            zx_handle_close(msg->handle[1]);
        } else if (r >= 0) {
            msg->datalen = r;
        }
        return r;
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
