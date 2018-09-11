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
#include <lib/fidl/coding.h>

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
    msg->extra_ptr = (zxrio_node_info_t*)FIDL_ALLOC_PRESENT;
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
        if (out_len <= (size_t)r) {
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

static zx_status_t fidl_node_clone(void* ctx, uint32_t flags, zx_handle_t object) {
    devhost_iostate_t* ios = ctx;
    flags = ios->flags | (flags & ZX_FS_FLAG_DESCRIBE);
    devhost_get_handles(object, ios->dev, NULL, flags);
    return ZX_OK;
}

static zx_status_t fidl_node_close(void* ctx, fidl_txn_t* txn) {
    devhost_iostate_t* ios = ctx;
    device_close(ios->dev, ios->flags);
    // The ios released its reference to this device by calling
    // device_close() Put an invalid pointer in its dev field to ensure any
    // use-after-release attempts explode.
    ios->dev = (void*) 0xdead;
    fuchsia_io_NodeClose_reply(txn, ZX_OK);
    return ERR_DISPATCHER_DONE;
}

static zx_status_t fidl_node_describe(void* ctx, fidl_txn_t* txn) {
    fprintf(stderr, "devhost: Object Describe not yet implemented\n");
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t fidl_directory_open(void* ctx, uint32_t flags, uint32_t mode,
                                       const char* path_data, size_t path_size,
                                       zx_handle_t object) {
    devhost_iostate_t* ios = ctx;
    zx_device_t* dev = ios->dev;
    if ((path_size < 1) || (path_size > 1024)) {
        zx_handle_close(object);
        return ZX_OK;
    }
    // TODO(smklein): Avoid assuming paths are null-terminated; this is only
    // safe because the path is the last secondary object in the DirectoryOpen
    // request.
    ((char*) path_data)[path_size] = 0;

    if (!strcmp(path_data, ".")) {
        path_data = NULL;
    }
    devhost_get_handles(object, dev, path_data, flags);
    return ZX_OK;
}

static zx_status_t fidl_directory_unlink(void* ctx, const char* path_data,
                                         size_t path_size, fidl_txn_t* txn) {
    return fuchsia_io_DirectoryUnlink_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

static zx_status_t fidl_directory_readdirents(void* ctx, uint64_t max_out, fidl_txn_t* txn) {
    return fuchsia_io_DirectoryReadDirents_reply(txn, ZX_ERR_NOT_SUPPORTED, NULL, 0);
}

static zx_status_t fidl_directory_rewind(void* ctx, fidl_txn_t* txn) {
    return fuchsia_io_DirectoryRewind_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

static zx_status_t fidl_directory_gettoken(void* ctx, fidl_txn_t* txn) {
    return fuchsia_io_DirectoryGetToken_reply(txn, ZX_ERR_NOT_SUPPORTED, ZX_HANDLE_INVALID);
}

static zx_status_t fidl_directory_rename(void* ctx, const char* src_data,
                                         size_t src_size, zx_handle_t dst_parent_token,
                                         const char* dst_data, size_t dst_size,
                                         fidl_txn_t* txn) {
    zx_handle_close(dst_parent_token);
    return fuchsia_io_DirectoryRename_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

static zx_status_t fidl_directory_link(void* ctx, const char* src_data,
                                       size_t src_size, zx_handle_t dst_parent_token,
                                       const char* dst_data, size_t dst_size,
                                       fidl_txn_t* txn) {
    zx_handle_close(dst_parent_token);
    return fuchsia_io_DirectoryLink_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

static const fuchsia_io_Directory_ops_t kDirectoryOps = {
    .Open = fidl_directory_open,
    .Unlink = fidl_directory_unlink,
    .ReadDirents = fidl_directory_readdirents,
    .Rewind = fidl_directory_rewind,
    .GetToken = fidl_directory_gettoken,
    .Rename = fidl_directory_rename,
    .Link = fidl_directory_link,
};

static zx_status_t fidl_file_read(void* ctx, uint64_t count, fidl_txn_t* txn) {
    devhost_iostate_t* ios = ctx;
    zx_device_t* dev = ios->dev;
    if (!CAN_READ(ios)) {
        return fuchsia_io_FileRead_reply(txn, ZX_ERR_ACCESS_DENIED, NULL, 0);
    } else if (count > ZXFIDL_MAX_MSG_BYTES) {
        return fuchsia_io_FileRead_reply(txn, ZX_ERR_INVALID_ARGS, NULL, 0);
    }

    uint8_t data[count];
    size_t actual = 0;
    zx_status_t r = do_sync_io(dev, DO_READ, data, count, ios->io_off);
    if (r >= 0) {
        ios->io_off += r;
        actual = r;
        r = ZX_OK;
    }
    return fuchsia_io_FileRead_reply(txn, r, data, actual);
}

static zx_status_t fidl_file_readat(void* ctx, uint64_t count, uint64_t offset, fidl_txn_t* txn) {
    devhost_iostate_t* ios = ctx;
    if (!CAN_READ(ios)) {
        return fuchsia_io_FileReadAt_reply(txn, ZX_ERR_ACCESS_DENIED, NULL, 0);
    } else if (count > ZXFIDL_MAX_MSG_BYTES) {
        return fuchsia_io_FileReadAt_reply(txn, ZX_ERR_INVALID_ARGS, NULL, 0);
    }

    uint8_t data[count];
    size_t actual = 0;
    zx_status_t r = do_sync_io(ios->dev, DO_READ, data, count, offset);
    if (r >= 0) {
        actual = r;
        r = ZX_OK;
    }
    return fuchsia_io_FileReadAt_reply(txn, r, data, actual);
}

static zx_status_t fidl_file_write(void* ctx, const uint8_t* data, size_t count, fidl_txn_t* txn) {
    devhost_iostate_t* ios = ctx;
    if (!CAN_WRITE(ios)) {
        return fuchsia_io_FileWrite_reply(txn, ZX_ERR_ACCESS_DENIED, 0);
    }
    size_t actual = 0;
    zx_status_t r = do_sync_io(ios->dev, DO_WRITE, (uint8_t*) data, count, ios->io_off);
    if (r >= 0) {
        ios->io_off += r;
        actual = r;
        r = ZX_OK;
    }
    return fuchsia_io_FileWrite_reply(txn, r, actual);
}

static zx_status_t fidl_file_writeat(void* ctx, const uint8_t* data, size_t count,
                                     uint64_t offset, fidl_txn_t* txn) {
    devhost_iostate_t* ios = ctx;
    if (!CAN_WRITE(ios)) {
        return fuchsia_io_FileWriteAt_reply(txn, ZX_ERR_ACCESS_DENIED, 0);
    }

    size_t actual = 0;
    zx_status_t r = do_sync_io(ios->dev, DO_WRITE, (uint8_t*) data, count, offset);
    if (r >= 0) {
        actual = r;
        r = ZX_OK;
    }
    return fuchsia_io_FileWriteAt_reply(txn, r, actual);
}

static zx_status_t fidl_file_seek(void* ctx, int64_t offset, fuchsia_io_SeekOrigin start,
                                  fidl_txn_t* txn) {
    devhost_iostate_t* ios = ctx;
    size_t end, n;
    end = dev_op_get_size(ios->dev);
    switch (start) {
    case fuchsia_io_SeekOrigin_START:
        if ((offset < 0) || ((size_t)offset > end)) {
            goto bad_args;
        }
        n = offset;
        break;
    case fuchsia_io_SeekOrigin_CURRENT:
        // TODO: track seekability with flag, don't update off
        // at all on read/write if not seekable
        n = ios->io_off + offset;
        if (offset < 0) {
            // if negative seek
            if (n > ios->io_off) {
                // wrapped around
                goto bad_args;
            }
        } else {
            // positive seek
            if (n < ios->io_off) {
                // wrapped around
                goto bad_args;
            }
        }
        break;
    case fuchsia_io_SeekOrigin_END:
        n = end + offset;
        if (offset <= 0) {
            // if negative or exact-end seek
            if (n > end) {
                // wrapped around
                goto bad_args;
            }
        } else {
            if (n < end) {
                // wrapped around
                goto bad_args;
            }
        }
        break;
    default:
        goto bad_args;
    }
    if (n > end) {
        // devices may not seek past the end
        goto bad_args;
    }
    ios->io_off = n;
    return fuchsia_io_FileSeek_reply(txn, ZX_OK, ios->io_off);

bad_args:
    return fuchsia_io_FileSeek_reply(txn, ZX_ERR_INVALID_ARGS, 0);
}

static zx_status_t fidl_file_truncate(void* ctx, uint64_t length, fidl_txn_t* txn) {
    return fuchsia_io_FileTruncate_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

static zx_status_t fidl_file_getflags(void* ctx, fidl_txn_t* txn) {
    return fuchsia_io_FileGetFlags_reply(txn, ZX_ERR_NOT_SUPPORTED, 0);
}

static zx_status_t fidl_file_setflags(void* ctx, uint32_t flags, fidl_txn_t* txn) {
    return fuchsia_io_FileSetFlags_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

static zx_status_t fidl_file_getvmo(void* ctx, uint32_t flags, fidl_txn_t* txn) {
    return fuchsia_io_FileGetVmo_reply(txn, ZX_ERR_NOT_SUPPORTED, ZX_HANDLE_INVALID);
}

static const fuchsia_io_File_ops_t kFileOps = {
    .Read = fidl_file_read,
    .ReadAt = fidl_file_readat,
    .Write = fidl_file_write,
    .WriteAt = fidl_file_writeat,
    .Seek = fidl_file_seek,
    .Truncate = fidl_file_truncate,
    .GetFlags = fidl_file_getflags,
    .SetFlags = fidl_file_setflags,
    .GetVmo = fidl_file_getvmo,
};

static zx_status_t fidl_node_sync(void* ctx, fidl_txn_t* txn) {
    devhost_iostate_t* ios = ctx;
    size_t actual;
    zx_status_t status = do_ioctl(ios->dev, IOCTL_DEVICE_SYNC, NULL, 0, NULL, 0, &actual);
    return fuchsia_io_NodeSync_reply(txn, status);
}

static zx_status_t fidl_node_getattr(void* ctx, fidl_txn_t* txn) {
    devhost_iostate_t* ios = ctx;
    fuchsia_io_NodeAttributes attributes;
    memset(&attributes, 0, sizeof(attributes));
    attributes.mode = V_TYPE_CDEV | V_IRUSR | V_IWUSR;
    attributes.content_size = dev_op_get_size(ios->dev);
    attributes.link_count = 1;
    return fuchsia_io_NodeGetAttr_reply(txn, ZX_OK, &attributes);
}

static zx_status_t fidl_node_setattr(void* ctx, uint32_t flags,
                                        const fuchsia_io_NodeAttributes* attributes,
                                        fidl_txn_t* txn) {
    return fuchsia_io_NodeSetAttr_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

static zx_status_t fidl_node_ioctl(void* ctx, uint32_t opcode, uint64_t max_out,
                                   const zx_handle_t* handles_data, size_t handles_count,
                                   const uint8_t* in_data, size_t in_count,
                                   fidl_txn_t* txn) {
    devhost_iostate_t* ios = ctx;
    char in_buf[FDIO_IOCTL_MAX_INPUT];
    size_t hsize = handles_count * sizeof(zx_handle_t);
    if ((in_count > FDIO_IOCTL_MAX_INPUT) || (max_out > ZXFIDL_MAX_MSG_BYTES)) {
        zx_handle_close_many(handles_data, handles_count);
        return fuchsia_io_NodeIoctl_reply(txn, ZX_ERR_INVALID_ARGS, NULL, 0,
                                          NULL, 0);
    }
    memcpy(in_buf, in_data, in_count);
    memcpy(in_buf, handles_data, hsize);

    uint8_t out[max_out];
    zx_handle_t* out_handles = (zx_handle_t*) out;
    size_t out_count = 0;
    zx_status_t r = do_ioctl(ios->dev, opcode, in_buf, in_count, out, max_out, &out_count);
    size_t out_hcount = 0;
    if (r >= 0) {
        switch (IOCTL_KIND(opcode)) {
        case IOCTL_KIND_GET_HANDLE:
            out_hcount = 1;
            break;
        case IOCTL_KIND_GET_TWO_HANDLES:
            out_hcount = 2;
            break;
        case IOCTL_KIND_GET_THREE_HANDLES:
            out_hcount = 3;
            break;
        default:
            out_hcount = 0;
            break;
        }
    }

    return fuchsia_io_NodeIoctl_reply(txn, r, out_handles, out_hcount, out, out_count);
}

static const fuchsia_io_Node_ops_t kNodeOps = {
    .Clone = fidl_node_clone,
    .Close = fidl_node_close,
    .Describe = fidl_node_describe,
    .Sync = fidl_node_sync,
    .GetAttr = fidl_node_getattr,
    .SetAttr = fidl_node_setattr,
    .Ioctl = fidl_node_ioctl,
};

zx_status_t devhost_fidl_handler(fidl_msg_t* msg, fidl_txn_t* txn, void* cookie) {
    fidl_message_header_t* hdr = (fidl_message_header_t*) msg->bytes;
    if (hdr->ordinal >= fuchsia_io_NodeCloneOrdinal &&
        hdr->ordinal <= fuchsia_io_NodeIoctlOrdinal) {
        return fuchsia_io_Node_dispatch(cookie, txn, msg, &kNodeOps);
    } else if (hdr->ordinal >= fuchsia_io_FileReadOrdinal &&
               hdr->ordinal <= fuchsia_io_FileGetVmoOrdinal) {
        return fuchsia_io_File_dispatch(cookie, txn, msg, &kFileOps);
    } else if (hdr->ordinal >= fuchsia_io_DirectoryOpenOrdinal &&
               hdr->ordinal <= fuchsia_io_DirectoryLinkOrdinal) {
        return fuchsia_io_Directory_dispatch(cookie, txn, msg, &kDirectoryOps);
    } else {
        devhost_iostate_t* ios = cookie;
        return dev_op_message(ios->dev, msg, txn);
    }
}
