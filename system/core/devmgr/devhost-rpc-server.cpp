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
#include <lib/zx/channel.h>
#include <zxcpp/new.h>

namespace devmgr {

#define ZXDEBUG 0

#define CAN_WRITE(ios) (ios->flags & ZX_FS_RIGHT_WRITABLE)
#define CAN_READ(ios) (ios->flags & ZX_FS_RIGHT_READABLE)

void describe_error(zx::channel h, zx_status_t status) {
    zxrio_describe_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.hdr.ordinal = fuchsia_io_NodeOnOpenOrdinal;
    msg.status = status;
    h.write(0, &msg, sizeof(msg), nullptr, 0);
}

static zx_status_t create_description(zx_device_t* dev, zxrio_describe_t* msg,
                                      zx_handle_t* handle) {
    memset(msg, 0, sizeof(*msg));
    msg->hdr.ordinal = fuchsia_io_NodeOnOpenOrdinal;
    msg->extra.tag = fuchsia_io_NodeInfoTag_device;
    msg->status = ZX_OK;
    msg->extra_ptr = (zxrio_node_info_t*)FIDL_ALLOC_PRESENT;
    *handle = ZX_HANDLE_INVALID;
    if (dev->event != ZX_HANDLE_INVALID) {
        zx_status_t r;
        if ((r = zx_handle_duplicate(dev->event, ZX_RIGHTS_BASIC,
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

static zx_status_t devhost_get_handles(zx::channel rh, zx_device_t* dev,
                                       const char* path, uint32_t flags) {
    zx_status_t r;
    // detect response directives and discard all other
    // protocol flags
    bool describe = flags & ZX_FS_FLAG_DESCRIBE;
    flags &= (~ZX_FS_FLAG_DESCRIBE);

    auto newios = fbl::make_unique<DevhostIostate>();
    if (!newios) {
        r = ZX_ERR_NO_MEMORY;
        if (describe) {
            describe_error(fbl::move(rh), r);
        }
        return r;
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
        r = rh.write(0, &info, sizeof(info), &handle, hcount);
        if (r != ZX_OK) {
            goto fail_open;
        }
    }

    // If we can't add the new ios and handle to the dispatcher our only option
    // is to give up and tear down.  In practice, this should never happen.
    if ((r = devhost_start_iostate(fbl::move(newios), fbl::move(rh))) != ZX_OK) {
        fprintf(stderr, "devhost_get_handles: failed to start iostate\n");
        // TODO(teisenbe/kulakowski): Should this be goto fail_open?
        goto fail;
    }
    return ZX_OK;

fail_open:
    device_close(dev, flags);
fail:
    if (describe) {
        describe_error(fbl::move(rh), r);
    }
    return r;
}

#define DO_READ 0
#define DO_WRITE 1

static ssize_t do_sync_io(zx_device_t* dev, uint32_t opcode, void* buf, size_t count, zx_off_t off) {
    size_t actual;
    zx_status_t r;
    if (opcode == DO_READ) {
        r = dev->Read(buf, count, off, &actual);
    } else {
        r = dev->Write(buf, count, off, &actual);
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
        char* drv_libname = in_len > 0 ? (char*)in_buf : nullptr;
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
        auto event = static_cast<zx_handle_t*>(out_buf);
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
        const char* name = dev->driver->name();
        if (name == nullptr) {
            name = "unknown";
        }
        size_t len = strlen(name);
        if (out_len <= len) {
            r = ZX_ERR_BUFFER_TOO_SMALL;
        } else {
            strncpy(static_cast<char*>(out_buf), name, len);
            *out_actual = len;
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
        if ((r = devhost_get_topo_path(dev, static_cast<char*>(out_buf), out_len, &actual)) < 0) {
            return r;
        }
        *out_actual = actual;
        return ZX_OK;
    }
    case IOCTL_DEVICE_DEBUG_SUSPEND: {
        return dev->Suspend(0);
    }
    case IOCTL_DEVICE_DEBUG_RESUME: {
        return dev->Resume(0);
    }
    case IOCTL_DEVICE_GET_DRIVER_LOG_FLAGS: {
        if (!dev->driver) {
            return ZX_ERR_UNAVAILABLE;
        }
        if (out_len < sizeof(uint32_t)) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        *((uint32_t *)out_buf) = dev->driver->driver_rec()->log_flags;
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
        dev->driver->driver_rec()->log_flags &= ~flags->clear;
        dev->driver->driver_rec()->log_flags |= flags->set;
        *out_actual = sizeof(driver_log_flags_t);
        return ZX_OK;
    }
    case IOCTL_DEVICE_UNBIND: {
        return device_unbind(dev);
    }
    default: {
        return dev->Ioctl(op, in_buf, in_len, out_buf, out_len, out_actual);
    }
    }
}

static zx_status_t fidl_node_clone(void* ctx, uint32_t flags, zx_handle_t object) {
    auto ios = static_cast<DevhostIostate*>(ctx);
    zx::channel c(object);
    flags = ios->flags | (flags & ZX_FS_FLAG_DESCRIBE);
    devhost_get_handles(fbl::move(c), ios->dev, nullptr, flags);
    return ZX_OK;
}

static zx_status_t fidl_node_close(void* ctx, fidl_txn_t* txn) {
    auto ios = static_cast<DevhostIostate*>(ctx);
    device_close(ios->dev, ios->flags);
    // The ios released its reference to this device by calling
    // device_close() Put an invalid pointer in its dev field to ensure any
    // use-after-release attempts explode.
    ios->dev = reinterpret_cast<zx_device_t*>(0xdead);
    fuchsia_io_NodeClose_reply(txn, ZX_OK);
    return ERR_DISPATCHER_DONE;
}

static zx_status_t fidl_node_describe(void* ctx, fidl_txn_t* txn) {
    auto ios = static_cast<DevhostIostate*>(ctx);
    zx_device_t* dev = ios->dev;
    fuchsia_io_NodeInfo info;
    memset(&info, 0, sizeof(info));
    info.tag = fuchsia_io_NodeInfoTag_device;
    if (dev->event != ZX_HANDLE_INVALID) {
        zx_status_t status = zx_handle_duplicate(
            dev->event, ZX_RIGHTS_BASIC, &info.device.event);
        if (status != ZX_OK) {
            return status;
        }
    }
    return fuchsia_io_NodeDescribe_reply(txn, &info);
}

static zx_status_t fidl_directory_open(void* ctx, uint32_t flags, uint32_t mode,
                                       const char* path_data, size_t path_size,
                                       zx_handle_t object) {
    zx::channel c(object);
    auto ios = static_cast<DevhostIostate*>(ctx);
    zx_device_t* dev = ios->dev;
    if ((path_size < 1) || (path_size > 1024)) {
        return ZX_OK;
    }
    // TODO(smklein): Avoid assuming paths are null-terminated; this is only
    // safe because the path is the last secondary object in the DirectoryOpen
    // request.
    ((char*) path_data)[path_size] = 0;

    if (!strcmp(path_data, ".")) {
        path_data = nullptr;
    }
    devhost_get_handles(fbl::move(c), dev, path_data, flags);
    return ZX_OK;
}

static zx_status_t fidl_directory_unlink(void* ctx, const char* path_data,
                                         size_t path_size, fidl_txn_t* txn) {
    return fuchsia_io_DirectoryUnlink_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

static zx_status_t fidl_directory_readdirents(void* ctx, uint64_t max_out, fidl_txn_t* txn) {
    return fuchsia_io_DirectoryReadDirents_reply(txn, ZX_ERR_NOT_SUPPORTED, nullptr, 0);
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

static zx_status_t fidl_directory_watch(void* ctx, uint32_t mask, uint32_t options,
                                        zx_handle_t watcher, fidl_txn_t* txn) {
    zx_handle_close(watcher);
    return fuchsia_io_DirectoryWatch_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

static const fuchsia_io_Directory_ops_t kDirectoryOps = []() {
    fuchsia_io_Directory_ops_t ops;
    ops.Open = fidl_directory_open;
    ops.Unlink = fidl_directory_unlink;
    ops.ReadDirents = fidl_directory_readdirents;
    ops.Rewind = fidl_directory_rewind;
    ops.GetToken = fidl_directory_gettoken;
    ops.Rename = fidl_directory_rename;
    ops.Link = fidl_directory_link;
    ops.Watch = fidl_directory_watch;
    return ops;
}();

static zx_status_t fidl_directory_admin_mount(void* ctx, zx_handle_t h, fidl_txn_t* txn) {
    zx_handle_close(h);
    return fuchsia_io_DirectoryAdminMount_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

static zx_status_t fidl_directory_admin_mount_and_create(void* ctx, zx_handle_t h,
                                                         const char* name, size_t len,
                                                         uint32_t flags, fidl_txn_t* txn) {
    zx_handle_close(h);
    return fuchsia_io_DirectoryAdminMountAndCreate_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

static zx_status_t fidl_directory_admin_unmount(void* ctx, fidl_txn_t* txn) {
    return fuchsia_io_DirectoryAdminUnmount_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

static zx_status_t fidl_directory_admin_unmount_node(void* ctx, fidl_txn_t* txn) {
    return fuchsia_io_DirectoryAdminUnmountNode_reply(txn, ZX_ERR_NOT_SUPPORTED,
                                                      ZX_HANDLE_INVALID);
}

static zx_status_t fidl_directory_admin_query_filesystem(void* ctx, fidl_txn_t* txn) {
    fuchsia_io_FilesystemInfo info;
    memset(&info, 0, sizeof(info));
    const char* devhost_name = "devfs:host";
    strlcpy((char*) info.name, devhost_name, fuchsia_io_MAX_FS_NAME_BUFFER);
    return fuchsia_io_DirectoryAdminQueryFilesystem_reply(txn, ZX_OK, &info);
}

static zx_status_t fidl_directory_admin_get_device_path(void* ctx, fidl_txn_t* txn) {
    return fuchsia_io_DirectoryAdminGetDevicePath_reply(txn, ZX_ERR_NOT_SUPPORTED,
                                                        NULL, 0);
}

static const fuchsia_io_DirectoryAdmin_ops_t kDirectoryAdminOps = []() {
    fuchsia_io_DirectoryAdmin_ops_t ops;
    ops.Mount = fidl_directory_admin_mount;
    ops.MountAndCreate = fidl_directory_admin_mount_and_create;
    ops.Unmount = fidl_directory_admin_unmount;
    ops.UnmountNode = fidl_directory_admin_unmount_node;
    ops.QueryFilesystem = fidl_directory_admin_query_filesystem;
    ops.GetDevicePath = fidl_directory_admin_get_device_path;
    return ops;
}();

static zx_status_t fidl_file_read(void* ctx, uint64_t count, fidl_txn_t* txn) {
    auto ios = static_cast<DevhostIostate*>(ctx);
    zx_device_t* dev = ios->dev;
    if (!CAN_READ(ios)) {
        return fuchsia_io_FileRead_reply(txn, ZX_ERR_ACCESS_DENIED, nullptr, 0);
    } else if (count > ZXFIDL_MAX_MSG_BYTES) {
        return fuchsia_io_FileRead_reply(txn, ZX_ERR_INVALID_ARGS, nullptr, 0);
    }

    uint8_t data[count];
    size_t actual = 0;
    zx_status_t status = ZX_OK;
    ssize_t r = do_sync_io(dev, DO_READ, data, count, ios->io_off);
    if (r >= 0) {
        ios->io_off += r;
        actual = r;
    } else {
        status = static_cast<zx_status_t>(r);
    }
    return fuchsia_io_FileRead_reply(txn, status, data, actual);
}

static zx_status_t fidl_file_readat(void* ctx, uint64_t count, uint64_t offset, fidl_txn_t* txn) {
    auto ios = static_cast<DevhostIostate*>(ctx);
    if (!CAN_READ(ios)) {
        return fuchsia_io_FileReadAt_reply(txn, ZX_ERR_ACCESS_DENIED, nullptr, 0);
    } else if (count > ZXFIDL_MAX_MSG_BYTES) {
        return fuchsia_io_FileReadAt_reply(txn, ZX_ERR_INVALID_ARGS, nullptr, 0);
    }

    uint8_t data[count];
    size_t actual = 0;
    zx_status_t status = ZX_OK;
    ssize_t r = do_sync_io(ios->dev, DO_READ, data, count, offset);
    if (r >= 0) {
        actual = r;
    } else {
        status = static_cast<zx_status_t>(r);
    }
    return fuchsia_io_FileReadAt_reply(txn, status, data, actual);
}

static zx_status_t fidl_file_write(void* ctx, const uint8_t* data, size_t count, fidl_txn_t* txn) {
    auto ios = static_cast<DevhostIostate*>(ctx);
    if (!CAN_WRITE(ios)) {
        return fuchsia_io_FileWrite_reply(txn, ZX_ERR_ACCESS_DENIED, 0);
    }
    size_t actual = 0;
    zx_status_t status = ZX_OK;
    ssize_t r = do_sync_io(ios->dev, DO_WRITE, (uint8_t*) data, count, ios->io_off);
    if (r >= 0) {
        ios->io_off += r;
        actual = r;
    } else {
        status = static_cast<zx_status_t>(r);
    }
    return fuchsia_io_FileWrite_reply(txn, status, actual);
}

static zx_status_t fidl_file_writeat(void* ctx, const uint8_t* data, size_t count,
                                     uint64_t offset, fidl_txn_t* txn) {
    auto ios = static_cast<DevhostIostate*>(ctx);
    if (!CAN_WRITE(ios)) {
        return fuchsia_io_FileWriteAt_reply(txn, ZX_ERR_ACCESS_DENIED, 0);
    }

    size_t actual = 0;
    zx_status_t status = ZX_OK;
    ssize_t r = do_sync_io(ios->dev, DO_WRITE, (uint8_t*) data, count, offset);
    if (r >= 0) {
        actual = r;
    } else {
        status = static_cast<zx_status_t>(r);
    }
    return fuchsia_io_FileWriteAt_reply(txn, status, actual);
}

static zx_status_t fidl_file_seek(void* ctx, int64_t offset, fuchsia_io_SeekOrigin start,
                                  fidl_txn_t* txn) {
    auto ios = static_cast<DevhostIostate*>(ctx);
    size_t end, n;
    end = ios->dev->GetSize();
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

static const fuchsia_io_File_ops_t kFileOps = []() {
    fuchsia_io_File_ops_t ops;
    ops.Read = fidl_file_read;
    ops.ReadAt = fidl_file_readat;
    ops.Write = fidl_file_write;
    ops.WriteAt = fidl_file_writeat;
    ops.Seek = fidl_file_seek;
    ops.Truncate = fidl_file_truncate;
    ops.GetFlags = fidl_file_getflags;
    ops.SetFlags = fidl_file_setflags;
    ops.GetVmo = fidl_file_getvmo;
    return ops;
}();

static zx_status_t fidl_node_sync(void* ctx, fidl_txn_t* txn) {
    auto ios = static_cast<DevhostIostate*>(ctx);
    size_t actual;
    ssize_t r = do_ioctl(ios->dev, IOCTL_DEVICE_SYNC, nullptr, 0, nullptr, 0, &actual);
    auto status = static_cast<zx_status_t>(r);
    return fuchsia_io_NodeSync_reply(txn, status);
}

static zx_status_t fidl_node_getattr(void* ctx, fidl_txn_t* txn) {
    auto ios = static_cast<DevhostIostate*>(ctx);
    fuchsia_io_NodeAttributes attributes;
    memset(&attributes, 0, sizeof(attributes));
    attributes.mode = V_TYPE_CDEV | V_IRUSR | V_IWUSR;
    attributes.content_size = ios->dev->GetSize();
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
    auto ios = static_cast<DevhostIostate*>(ctx);
    char in_buf[FDIO_IOCTL_MAX_INPUT];
    size_t hsize = handles_count * sizeof(zx_handle_t);
    if ((in_count > FDIO_IOCTL_MAX_INPUT) || (max_out > ZXFIDL_MAX_MSG_BYTES)) {
        zx_handle_close_many(handles_data, handles_count);
        return fuchsia_io_NodeIoctl_reply(txn, ZX_ERR_INVALID_ARGS, nullptr, 0,
                                          nullptr, 0);
    }
    memcpy(in_buf, in_data, in_count);
    memcpy(in_buf, handles_data, hsize);

    uint8_t out[max_out];
    zx_handle_t* out_handles = (zx_handle_t*) out;
    size_t out_count = 0;
    ssize_t r = do_ioctl(ios->dev, opcode, in_buf, in_count, out, max_out, &out_count);
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

    auto status = static_cast<zx_status_t>(r);
    return fuchsia_io_NodeIoctl_reply(txn, status, out_handles, out_hcount, out, out_count);
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
               hdr->ordinal <= fuchsia_io_DirectoryWatchOrdinal) {
        return fuchsia_io_Directory_dispatch(cookie, txn, msg, &kDirectoryOps);
    } else if (hdr->ordinal >= fuchsia_io_DirectoryAdminMountOrdinal &&
               hdr->ordinal <= fuchsia_io_DirectoryAdminGetDevicePathOrdinal) {
        return fuchsia_io_DirectoryAdmin_dispatch(cookie, txn, msg, &kDirectoryAdminOps);
    } else {
        auto ios = static_cast<DevhostIostate*>(cookie);
        return ios->dev->Message(msg, txn);
    }
}

} // namespace devmgr
