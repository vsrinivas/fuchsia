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

#include <lib/sync/completion.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include <zircon/device/device.h>
#include <zircon/device/vfs.h>

#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/debug.h>
#include <lib/fdio/io.h>
#include <lib/fdio/vfs.h>

#define ZXDEBUG 0

#define CAN_WRITE(ios) (ios->flags & ZX_FS_RIGHT_WRITABLE)
#define CAN_READ(ios) (ios->flags & ZX_FS_RIGHT_READABLE)

void describe_error(zx_handle_t h, zx_status_t status) {
    zxrio_describe_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.op = ZXFIDL_ON_OPEN;
    msg.status = status;
    zx_channel_write(h, 0, &msg, sizeof(msg), NULL, 0);
    zx_handle_close(h);
}

static zx_status_t create_description(zx_device_t* dev, zxrio_describe_t* msg,
                                      zx_handle_t* handle) {
    memset(msg, 0, sizeof(*msg));
    msg->op = ZXFIDL_ON_OPEN;
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

static ssize_t do_ioctl(zx_device_t* dev, uint32_t op, const void* in_buf, size_t in_len,
                        void* out_buf, size_t out_len, size_t* out_actual) {
    zx_status_t r;
    switch (op) {
    case IOCTL_DEVICE_BIND: {
        char* drv_libname = in_len > 0 ? (char*)in_buf : NULL;
        if (in_len > PATH_MAX) {
            return ZX_ERR_BAD_PATH;
        }
        drv_libname[in_len] = 0;
        if ((r = device_bind(dev, drv_libname) < 0)) {
            return r;
        }
        *out_actual = r;
        return ZX_OK;
    }
    case IOCTL_DEVICE_GET_EVENT_HANDLE: {
        if (out_len < sizeof(zx_handle_t)) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        zx_handle_t* event = out_buf;
        if ((r = zx_handle_duplicate(dev->event, ZX_RIGHTS_BASIC | ZX_RIGHT_READ, event)) != ZX_OK) {
            return r;
        }
        *out_actual = sizeof(zx_handle_t);
        return ZX_OK;
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
            *out_actual = r;
            r = ZX_OK;
        }
        return r;
    }
    case IOCTL_DEVICE_GET_DEVICE_NAME: {
        size_t actual = strlen(dev->name) + 1;
        if (out_len < actual) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        memcpy(out_buf, dev->name, actual);
        *out_actual = actual;
        return ZX_OK;
    }
    case IOCTL_DEVICE_GET_TOPO_PATH: {
        size_t actual;
        if ((r = devhost_get_topo_path(dev, out_buf, out_len, &actual)) < 0) {
            return r;
        }
        *out_actual = actual;
        return ZX_OK;
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
        *out_actual = sizeof(vfs_query_info_t) + strlen(devhost_name);
        return ZX_OK;
    }
    case IOCTL_DEVICE_GET_DRIVER_LOG_FLAGS: {
        if (!dev->driver) {
            return ZX_ERR_UNAVAILABLE;
        }
        if (out_len < sizeof(uint32_t)) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        *((uint32_t *)out_buf) = dev->driver->driver_rec->log_flags;
        *out_actual = sizeof(uint32_t);
        return ZX_OK;
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
        *out_actual = sizeof(driver_log_flags_t);
        return ZX_OK;
    }
    default: {
        return dev_op_ioctl(dev, op, in_buf, in_len, out_buf, out_len, out_actual);
    }
    }
}

static void discard_handles(zx_handle_t* handles, size_t count) {
    while (count-- > 0) {
        zx_handle_close(*handles++);
    }
}

zx_status_t devhost_rio_handler(fidl_msg_t* msg, void* cookie) {
    fidl_message_header_t* hdr = (fidl_message_header_t*) msg->bytes;
    devhost_iostate_t* ios = cookie;
    zx_device_t* dev = ios->dev;
    switch (hdr->ordinal) {
    case ZXFIDL_CLOSE:
        device_close(dev, ios->flags);
        // The ios released its reference to this device by calling device_close()
        // Put an invalid pointer in its dev field to ensure any use-after-release
        // attempts explode.
        ios->dev = (void*) 0xdead;
        return ZX_OK;
    case ZXFIDL_OPEN: {
        fuchsia_io_DirectoryOpenRequest* request = (fuchsia_io_DirectoryOpenRequest*) hdr;

        uint32_t len = request->path.size;
        zx_handle_t h = request->object;
        char* name = request->path.data;
        uint32_t flags = request->flags;

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
    case ZXFIDL_CLONE: {
        fuchsia_io_ObjectCloneRequest* request = (fuchsia_io_ObjectCloneRequest*) hdr;
        zx_handle_t h = request->object;
        uint32_t flags = request->flags;
        flags = ios->flags | (flags & ZX_FS_FLAG_DESCRIBE);
        devhost_get_handles(h, dev, NULL, flags);
        return ERR_DISPATCHER_INDIRECT;
    }
    case ZXFIDL_READ: {
        if (!CAN_READ(ios)) {
            return ZX_ERR_ACCESS_DENIED;
        }
        fuchsia_io_FileReadRequest* request = (fuchsia_io_FileReadRequest*) hdr;
        fuchsia_io_FileReadResponse* response = (fuchsia_io_FileReadResponse*) hdr;
        void* data = (void*)((uintptr_t)(response) +
                FIDL_ALIGN(sizeof(fuchsia_io_FileReadResponse)));
        uint32_t len = request->count;

        zx_status_t r = do_sync_io(dev, DO_READ, data, len, ios->io_off);
        if (r >= 0) {
            ios->io_off += r;
            response->data.count = r;
            r = ZX_OK;
        }
        return r;
    }
    case ZXFIDL_READ_AT: {
        if (!CAN_READ(ios)) {
            return ZX_ERR_ACCESS_DENIED;
        }
        fuchsia_io_FileReadAtRequest* request = (fuchsia_io_FileReadAtRequest*) hdr;
        fuchsia_io_FileReadAtResponse* response = (fuchsia_io_FileReadAtResponse*) hdr;
        void* data = (void*)((uintptr_t)(response) +
                     FIDL_ALIGN(sizeof(fuchsia_io_FileReadAtResponse)));
        uint32_t len = request->count;
        uint64_t offset = request->offset;
        zx_status_t r = do_sync_io(dev, DO_READ, data, len, offset);

        response->data.count = r;
        return r > 0 ? ZX_OK : r;
    }
    case ZXFIDL_WRITE: {
        if (!CAN_WRITE(ios)) {
            return ZX_ERR_ACCESS_DENIED;
        }
        fuchsia_io_FileWriteRequest* request = (fuchsia_io_FileWriteRequest*) hdr;
        fuchsia_io_FileWriteResponse* response = (fuchsia_io_FileWriteResponse*) hdr;
        void* data = request->data.data;
        uint32_t len = request->data.count;

        zx_status_t r = do_sync_io(dev, DO_WRITE, data, len, ios->io_off);
        if (r >= 0) {
            ios->io_off += r;
            response->actual = r;
            r = ZX_OK;
        }
        return r;
    }
    case ZXFIDL_WRITE_AT: {
        if (!CAN_WRITE(ios)) {
            return ZX_ERR_ACCESS_DENIED;
        }
        fuchsia_io_FileWriteAtRequest* request = (fuchsia_io_FileWriteAtRequest*) hdr;
        fuchsia_io_FileWriteAtResponse* response = (fuchsia_io_FileWriteAtResponse*) hdr;
        void* data = request->data.data;
        uint32_t len = request->data.count;
        uint64_t offset = request->offset;

        zx_status_t r = do_sync_io(dev, DO_WRITE, data, len, offset);
        response->actual = r > 0 ? r : 0;
        return r > 0 ? ZX_OK : r;
    }
    case ZXFIDL_SEEK: {
        fuchsia_io_FileSeekRequest* request = (fuchsia_io_FileSeekRequest*) hdr;
        fuchsia_io_FileSeekResponse* response = (fuchsia_io_FileSeekResponse*) hdr;

        static_assert(SEEK_SET == fuchsia_io_SeekOrigin_Start, "");
        static_assert(SEEK_CUR == fuchsia_io_SeekOrigin_Current, "");
        static_assert(SEEK_END == fuchsia_io_SeekOrigin_End, "");

        off_t offset = request->offset;
        int whence = request->start;

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
        response->offset = ios->io_off;
        return ZX_OK;
    }
    case ZXFIDL_STAT: {
        fuchsia_io_NodeGetAttrResponse* response = (fuchsia_io_NodeGetAttrResponse*) hdr;
        memset(&response->attributes, 0, sizeof(response->attributes));
        response->attributes.mode = V_TYPE_CDEV | V_IRUSR | V_IWUSR;
        response->attributes.content_size = dev_op_get_size(dev);
        response->attributes.link_count = 1;
        return ZX_OK;
    }
    case ZXFIDL_SYNC: {
        size_t actual;
        return do_ioctl(dev, IOCTL_DEVICE_SYNC, NULL, 0, NULL, 0, &actual);
    }
    case ZXFIDL_IOCTL: {
        fuchsia_io_NodeIoctlRequest* request = (fuchsia_io_NodeIoctlRequest*) hdr;
        fuchsia_io_NodeIoctlResponse* response = (fuchsia_io_NodeIoctlResponse*) hdr;

        char in_buf[FDIO_IOCTL_MAX_INPUT];
        size_t hsize = request->handles.count * sizeof(zx_handle_t);
        if (hsize + request->in.count > FDIO_IOCTL_MAX_INPUT) {
            discard_handles(request->handles.data, request->handles.count);
            return ZX_ERR_INVALID_ARGS;
        }
        memcpy(in_buf, request->in.data, request->in.count);
        memcpy(in_buf, request->handles.data, hsize);

        uint32_t op = request->opcode;
        void* secondary = (void*)((uintptr_t)(hdr) +
                FIDL_ALIGN(sizeof(fuchsia_io_NodeIoctlResponse)));
        response->out.count = 0;
        zx_status_t r = do_ioctl(dev, op, in_buf, request->in.count,
                                 secondary, request->max_out, &response->out.count);
        if (r >= 0) {
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

        // FIDL messages expect to receive "handles" in the secondary object,
        // followed by "data". Although the space for "handles" is duplicated
        // in the "data" field, both secondary objects must be present if
        // any handles are returned.
        response->handles.data = secondary;
        response->out.data = secondary + FIDL_ALIGN(sizeof(zx_handle_t) * response->handles.count);
        if (response->handles.count > 0) {
            memmove(response->out.data, secondary, response->out.count);
        }
        return r;
    }
    default:
        // close inbound handles so they do not leak
        zx_handle_close_many(msg->handles, msg->num_handles);
        return ZX_ERR_NOT_SUPPORTED;
    }
}
