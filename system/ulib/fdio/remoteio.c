// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <threads.h>

#include <zircon/assert.h>
#include <zircon/device/device.h>
#include <zircon/device/ioctl.h>
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/debug.h>
#include <lib/fdio/io.h>
#include <lib/fdio/namespace.h>
#include <lib/fdio/remoteio.h>
#include <lib/fdio/util.h>
#include <lib/fdio/vfs.h>

#include "private-fidl.h"
#include "private-remoteio.h"

#define ZXDEBUG 0

// POLL_MASK and POLL_SHIFT intend to convert the lower five POLL events into
// ZX_USER_SIGNALs and vice-versa. Other events need to be manually converted to
// a zx_signals_t, if they are desired.
#define POLL_SHIFT  24
#define POLL_MASK   0x1F

static_assert(ZX_USER_SIGNAL_0 == (1 << POLL_SHIFT), "");
static_assert((POLLIN << POLL_SHIFT) == DEVICE_SIGNAL_READABLE, "");
static_assert((POLLPRI << POLL_SHIFT) == DEVICE_SIGNAL_OOB, "");
static_assert((POLLOUT << POLL_SHIFT) == DEVICE_SIGNAL_WRITABLE, "");
static_assert((POLLERR << POLL_SHIFT) == DEVICE_SIGNAL_ERROR, "");
static_assert((POLLHUP << POLL_SHIFT) == DEVICE_SIGNAL_HANGUP, "");

static void discard_handles(zx_handle_t* handles, unsigned count) {
    while (count-- > 0) {
        zx_handle_close(*handles++);
    }
}

zx_status_t zxrio_handle_rpc(zx_handle_t h, zxrio_cb_t cb, void* cookie) {
    char bytes[ZXFIDL_MAX_MSG_BYTES];
    zx_handle_t handles[ZXFIDL_MAX_MSG_HANDLES];
    fidl_msg_t msg = {
        .bytes = bytes,
        .handles = handles,
        .num_bytes = countof(bytes),
        .num_handles = countof(handles),
    };
    zx_status_t r = zxrio_read_request(h, &msg);
    if (r != ZX_OK) {
        return r;
    }
    const fidl_message_header_t* hdr = (fidl_message_header_t*) msg.bytes;
    bool is_close = (hdr->ordinal == ZXFIDL_CLOSE);

    r = cb(&msg, cookie);
    switch (r) {
    case ERR_DISPATCHER_INDIRECT:
        // callback is handling the reply itself
        // and took ownership of the reply handle
        return ZX_OK;
    case ERR_DISPATCHER_ASYNC:
        // Same as the indirect case, but also identify that
        // the callback will asynchronously re-trigger the
        // dispatcher.
        return ERR_DISPATCHER_ASYNC;
    }

    r = zxrio_write_response(h, r, &msg);

    if (is_close) {
        // signals to not perform a close callback
        return ERR_DISPATCHER_DONE;
    } else {
        return r;
    }
}

zx_status_t zxrio_handle_close(zxrio_cb_t cb, void* cookie) {
    fuchsia_io_ObjectCloseRequest request;
    memset(&request, 0, sizeof(request));
    request.hdr.ordinal = ZXFIDL_CLOSE;
    fidl_msg_t msg = {
        .bytes = &request,
        .handles = NULL,
        .num_bytes = sizeof(request),
        .num_handles = 0u,
    };

    // Remote side was closed.
    cb(&msg, cookie);
    return ERR_DISPATCHER_DONE;
}

zx_status_t zxrio_handler(zx_handle_t h, zxrio_cb_t cb, void* cookie) {
    if (h == ZX_HANDLE_INVALID) {
        return zxrio_handle_close(cb, cookie);
    } else {
        return zxrio_handle_rpc(h, cb, cookie);
    }
}

zx_handle_t zxrio_handle(zxrio_t* rio) {
    return rio->h;
}

zx_status_t zxrio_object_extract_handle(const zxrio_object_info_t* info,
                                        zx_handle_t* out) {
    switch (info->tag) {
    case FDIO_PROTOCOL_FILE:
        if (info->file.e != ZX_HANDLE_INVALID) {
            *out = info->file.e;
            return ZX_OK;
        }
        break;
    case FDIO_PROTOCOL_SOCKET_CONNECTED:
    case FDIO_PROTOCOL_SOCKET:
        if (info->socket.s != ZX_HANDLE_INVALID) {
            *out = info->socket.s;
            return ZX_OK;
        }
        break;
    case FDIO_PROTOCOL_PIPE:
        if (info->pipe.s != ZX_HANDLE_INVALID) {
            *out = info->pipe.s;
            return ZX_OK;
        }
        break;
    case FDIO_PROTOCOL_VMOFILE:
        if (info->vmofile.v != ZX_HANDLE_INVALID) {
            *out = info->vmofile.v;
            return ZX_OK;
        }
        break;
    case FDIO_PROTOCOL_DEVICE:
        if (info->device.e != ZX_HANDLE_INVALID) {
            *out = info->device.e;
            return ZX_OK;
        }
        break;
    }
    return ZX_ERR_NOT_FOUND;
}

zx_status_t zxrio_close(fdio_t* io) {
    zxrio_t* rio = (zxrio_t*)io;

    zx_status_t r = fidl_close(rio);
    zx_handle_t h = rio->h;
    rio->h = 0;
    zx_handle_close(h);
    if (rio->h2 > 0) {
        h = rio->h2;
        rio->h2 = 0;
        zx_handle_close(h);
    }
    return r;
}

// Synchronously (non-pipelined) open an object
// The svc handle is only used to send a message
static zx_status_t zxrio_sync_open_connection(zx_handle_t svc, uint32_t op,
                                              uint32_t flags, uint32_t mode,
                                              const char* path, size_t pathlen,
                                              zxrio_describe_t* info, zx_handle_t* out) {
    if (!(flags & ZX_FS_FLAG_DESCRIBE)) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t r;
    zx_handle_t h;
    zx_handle_t cnxn;
    if ((r = zx_channel_create(0, &h, &cnxn)) != ZX_OK) {
        return r;
    }

    switch (op) {
    case ZXFIDL_CLONE:
        r = fidl_clone_request(svc, cnxn, flags);
        break;
    case ZXFIDL_OPEN:
        r = fidl_open_request(svc, cnxn, flags, mode, path, pathlen);
        break;
    default:
        zx_handle_close(cnxn);
        r = ZX_ERR_NOT_SUPPORTED;
    }

    if (r != ZX_OK) {
        zx_handle_close(h);
        return r;
    }

    if ((r = zxrio_process_open_response(h, info)) != ZX_OK) {
        zx_handle_close(h);
        return r;
    }
    *out = h;
    return ZX_OK;
}

// Open an object without waiting for the response.
// This function always consumes the cnxn handle
// The svc handle is only used to send a message
static zx_status_t zxrio_connect(zx_handle_t svc, zx_handle_t cnxn,
                                 uint32_t op, uint32_t flags, uint32_t mode,
                                 const char* name) {
    size_t len = strlen(name);
    if (len >= PATH_MAX) {
        zx_handle_close(cnxn);
        return ZX_ERR_BAD_PATH;
    }
    if (flags & ZX_FS_FLAG_DESCRIBE) {
        zx_handle_close(cnxn);
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t r;
    switch (op) {
    case ZXFIDL_CLONE:
        r = fidl_clone_request(svc, cnxn, flags);
        break;
    case ZXFIDL_OPEN:
        r = fidl_open_request(svc, cnxn, flags, mode, name, len);
        break;
    default:
        zx_handle_close(cnxn);
        r = ZX_ERR_NOT_SUPPORTED;
    }
    return r;
}

static ssize_t zxrio_write(fdio_t* io, const void* data, size_t len) {
    zxrio_t* rio = (zxrio_t*) io;
    zx_status_t status = ZX_OK;
    uint64_t count = 0;
    uint64_t xfer;
    while (len > 0) {
        xfer = (len > FDIO_CHUNK_SIZE) ? FDIO_CHUNK_SIZE : len;
        uint64_t actual = 0;
        if ((status = fidl_write(rio, data, xfer, &actual)) != ZX_OK) {
            return status;
        }
        count += actual;
        data += actual;
        len -= actual;
        if (xfer != actual) {
            break;
        }
    }
    if (count == 0) {
        return status;
    }
    return count;
}

static ssize_t zxrio_write_at(fdio_t* io, const void* data, size_t len, off_t offset) {
    zxrio_t* rio = (zxrio_t*) io;
    zx_status_t status = ZX_ERR_IO;
    uint64_t count = 0;
    uint64_t xfer;
    while (len > 0) {
        xfer = (len > FDIO_CHUNK_SIZE) ? FDIO_CHUNK_SIZE : len;
        uint64_t actual = 0;
        if ((status = fidl_writeat(rio, data, xfer, offset, &actual)) != ZX_OK) {
            return status;
        }
        count += actual;
        data += actual;
        offset += actual;
        len -= actual;
        if (xfer != actual) {
            break;
        }
    }
    if (count == 0) {
        return status;
    }
    return count;
}

static ssize_t zxrio_read(fdio_t* io, void* data, size_t len) {
    zxrio_t* rio = (zxrio_t*) io;
    zx_status_t status;
    uint64_t count = 0;
    uint64_t xfer;
    while (len > 0) {
        xfer = (len > FDIO_CHUNK_SIZE) ? FDIO_CHUNK_SIZE : len;
        uint64_t actual = 0;
        if ((status = fidl_read(rio, data, xfer, &actual)) != ZX_OK) {
            return status;
        }
        count += actual;
        data += actual;
        len -= actual;
        if (xfer != actual) {
            break;
        }
    }
    if (count == 0) {
        return status;
    }
    return count;
}

static ssize_t zxrio_read_at(fdio_t* io, void* data, size_t len, off_t offset) {
    zxrio_t* rio = (zxrio_t*) io;
    zx_status_t status;
    uint64_t count = 0;
    uint64_t xfer;
    while (len > 0) {
        xfer = (len > FDIO_CHUNK_SIZE) ? FDIO_CHUNK_SIZE : len;
        uint64_t actual = 0;
        if ((status = fidl_readat(rio, data, xfer, offset, &actual)) != ZX_OK) {
            return status;
        }
        offset += actual;
        count += actual;
        data += actual;
        len -= actual;
        if (xfer != actual) {
            break;
        }
    }
    if (count == 0) {
        return status;
    }
    return count;
}

static off_t zxrio_seek(fdio_t* io, off_t offset, int whence) {
    zxrio_t* rio = (zxrio_t*)io;
    zx_status_t status = fidl_seek(rio, offset, whence, &offset);
    if (status != ZX_OK) {
        return status;
    }
    return offset;
}

ssize_t zxrio_ioctl(fdio_t* io, uint32_t op, const void* in_buf,
                    size_t in_len, void* out_buf, size_t out_len) {
    zxrio_t* rio = (zxrio_t*)io;
    if (in_len > FDIO_IOCTL_MAX_INPUT || out_len > FDIO_CHUNK_SIZE) {
        return ZX_ERR_INVALID_ARGS;
    }
    size_t actual;
    zx_status_t status = fidl_ioctl(rio, op, in_buf, in_len, out_buf, out_len, &actual);
    if (status != ZX_OK) {
        return status;
    }
    return actual;
}

// Takes ownership of the optional |extra_handle|.
//
// Decodes the handle into |info|, if it exists and should
// be decoded.
static zx_status_t zxrio_decode_describe_handle(zxrio_describe_t* info,
                                                zx_handle_t extra_handle) {
    bool have_handle = (extra_handle != ZX_HANDLE_INVALID);
    bool want_handle = false;
    zx_handle_t* handle_target = NULL;

    switch (info->extra.tag) {
    // Case: No extra handles expected
    case FDIO_PROTOCOL_SERVICE:
    case FDIO_PROTOCOL_DIRECTORY:
        break;
    // Case: Extra handles optional
    case FDIO_PROTOCOL_FILE:
        handle_target = &info->extra.file.e;
        goto handle_optional;
    case FDIO_PROTOCOL_DEVICE:
        handle_target = &info->extra.device.e;
        goto handle_optional;
    case FDIO_PROTOCOL_SOCKET:
        handle_target = &info->extra.socket.s;
        goto handle_optional;
handle_optional:
        want_handle = *handle_target == FIDL_HANDLE_PRESENT;
        break;
    // Case: Extra handles required
    case FDIO_PROTOCOL_PIPE:
        handle_target = &info->extra.pipe.s;
        goto handle_required;
    case FDIO_PROTOCOL_VMOFILE:
        handle_target = &info->extra.vmofile.v;
        goto handle_required;
handle_required:
        want_handle = *handle_target == FIDL_HANDLE_PRESENT;
        if (!want_handle) {
            goto fail;
        }
        break;
    default:
        printf("Unexpected protocol type opening connection\n");
        goto fail;
    }

    if (have_handle != want_handle) {
        goto fail;
    }
    if (have_handle) {
        *handle_target = extra_handle;
    }
    return ZX_OK;

fail:
    if (have_handle) {
        zx_handle_close(extra_handle);
    }
    return ZX_ERR_IO;
}

zx_status_t zxrio_process_open_response(zx_handle_t h, zxrio_describe_t* info) {
    zx_object_wait_one(h, ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                       ZX_TIME_INFINITE, NULL);

    // Attempt to read the description from open
    uint32_t dsize = sizeof(*info);
    zx_handle_t extra_handle = ZX_HANDLE_INVALID;
    uint32_t actual_handles;
    zx_status_t r = zx_channel_read(h, 0, info, &extra_handle, dsize, 1, &dsize,
                                    &actual_handles);
    if (r != ZX_OK) {
        return r;
    }
    if (dsize < ZXRIO_DESCRIBE_HDR_SZ || info->op != ZXFIDL_ON_OPEN) {
        r = ZX_ERR_IO;
    } else {
        r = info->status;
    }

    if (dsize != sizeof(zxrio_describe_t)) {
        r = (r != ZX_OK) ? r : ZX_ERR_IO;
    }

    if (r != ZX_OK) {
        if (extra_handle != ZX_HANDLE_INVALID) {
            zx_handle_close(extra_handle);
        }
        return r;
    }

    // Confirm that the objects "zxrio_describe_t" and "fuchsia_io_ObjectOnOpenEvent"
    // are aligned enough to be compatible.
    //
    // This is somewhat complicated by the fact that the "fuchsia_io_ObjectOnOpenEvent"
    // object has an optional "fuchsia_io_ObjectInfo" secondary which exists immediately
    // following the struct.
    static_assert(__builtin_offsetof(zxrio_describe_t, extra) ==
                  FIDL_ALIGN(sizeof(fuchsia_io_ObjectOnOpenEvent)),
                  "RIO Description message doesn't align with FIDL response secondary");
    static_assert(sizeof(zxrio_object_info_t) == sizeof(fuchsia_io_ObjectInfo),
                  "RIO Object Info doesn't align with FIDL object info");
    static_assert(__builtin_offsetof(zxrio_object_info_t, file.e) ==
                  __builtin_offsetof(fuchsia_io_ObjectInfo, file.event), "Unaligned File");
    static_assert(__builtin_offsetof(zxrio_object_info_t, pipe.s) ==
                  __builtin_offsetof(fuchsia_io_ObjectInfo, pipe.socket), "Unaligned Pipe");
    static_assert(__builtin_offsetof(zxrio_object_info_t, vmofile.v) ==
                  __builtin_offsetof(fuchsia_io_ObjectInfo, vmofile.vmo), "Unaligned Vmofile");
    static_assert(__builtin_offsetof(zxrio_object_info_t, device.e) ==
                  __builtin_offsetof(fuchsia_io_ObjectInfo, device.event), "Unaligned Device");

    return zxrio_decode_describe_handle(info, extra_handle);
}

zx_status_t fdio_service_connect(const char* svcpath, zx_handle_t h) {
    if (svcpath == NULL) {
        zx_handle_close(h);
        return ZX_ERR_INVALID_ARGS;
    }
    // Otherwise attempt to connect through the root namespace
    if (fdio_root_ns != NULL) {
        return fdio_ns_connect(fdio_root_ns, svcpath,
                               ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE, h);
    }
    // Otherwise we fail
    zx_handle_close(h);
    return ZX_ERR_NOT_FOUND;
}

zx_status_t fdio_service_connect_at(zx_handle_t dir, const char* path, zx_handle_t h) {
    if (path == NULL) {
        zx_handle_close(h);
        return ZX_ERR_INVALID_ARGS;
    }
    if (dir == ZX_HANDLE_INVALID) {
        zx_handle_close(h);
        return ZX_ERR_UNAVAILABLE;
    }
    return zxrio_connect(dir, h, ZXFIDL_OPEN, ZX_FS_RIGHT_READABLE |
                         ZX_FS_RIGHT_WRITABLE, 0755, path);
}

zx_status_t fdio_open_at(zx_handle_t dir, const char* path, uint32_t flags, zx_handle_t h) {
    if (path == NULL) {
        zx_handle_close(h);
        return ZX_ERR_INVALID_ARGS;
    }
    if (dir == ZX_HANDLE_INVALID) {
        zx_handle_close(h);
        return ZX_ERR_UNAVAILABLE;
    }
    return zxrio_connect(dir, h, ZXFIDL_OPEN, flags, 0755, path);
}


zx_handle_t fdio_service_clone(zx_handle_t svc) {
    zx_handle_t cli, srv;
    zx_status_t r;
    if (svc == ZX_HANDLE_INVALID) {
        return ZX_HANDLE_INVALID;
    }
    if ((r = zx_channel_create(0, &cli, &srv)) < 0) {
        return ZX_HANDLE_INVALID;
    }
    if ((r = zxrio_connect(svc, srv, ZXFIDL_CLONE, ZX_FS_RIGHT_READABLE |
                           ZX_FS_RIGHT_WRITABLE, 0755, "")) < 0) {
        zx_handle_close(cli);
        return ZX_HANDLE_INVALID;
    }
    return cli;
}

zx_status_t fdio_service_clone_to(zx_handle_t svc, zx_handle_t srv) {
    if (srv == ZX_HANDLE_INVALID) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (svc == ZX_HANDLE_INVALID) {
        zx_handle_close(srv);
        return ZX_ERR_INVALID_ARGS;
    }
    return zxrio_connect(svc, srv, ZXFIDL_CLONE, ZX_FS_RIGHT_READABLE |
                         ZX_FS_RIGHT_WRITABLE, 0755, "");
}

zx_status_t zxrio_misc(fdio_t* io, uint32_t op, int64_t off,
                       uint32_t maxreply, void* ptr, size_t len) {
    zxrio_t* rio = (zxrio_t*)io;
    zx_status_t r;

    // Reroute FIDL operations
    switch (op) {
    case ZXFIDL_STAT: {
        size_t out_sz;
        if ((r = fidl_stat(rio, maxreply, ptr, &out_sz)) != ZX_OK) {
            return r;
        }
        return out_sz;
    }
    case ZXFIDL_SETATTR: {
        if (len != sizeof(vnattr_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        return fidl_setattr(rio, (const vnattr_t*) ptr);
    }
    case ZXFIDL_SYNC: {
        return fidl_sync(rio);
    }
    case ZXFIDL_READDIR: {
        switch (off) {
        case READDIR_CMD_RESET:
            if ((r = fidl_rewind(rio)) != ZX_OK) {
                return r;
            }
            __FALLTHROUGH;
        case READDIR_CMD_NONE: {
            size_t out_sz;
            if ((r = fidl_readdirents(rio, ptr, maxreply, &out_sz)) != ZX_OK) {
                return r;
            }
            return out_sz;
        }
        default:
            return ZX_ERR_INVALID_ARGS;
        }
    }
    case ZXFIDL_UNLINK: {
        return fidl_unlink(rio, ptr, len);
    }
    case ZXFIDL_TRUNCATE: {
        return fidl_truncate(rio, off);
    }
    case ZXFIDL_RENAME: {
        size_t srclen = strlen(ptr);
        size_t dstlen = len - (srclen + 2);
        const char* src = ptr;
        const char* dst = ptr + srclen + 1;
        return fidl_rename(rio, src, srclen, (zx_handle_t) off, dst, dstlen);
    }
    case ZXFIDL_LINK: {
        size_t srclen = strlen(ptr);
        size_t dstlen = len - (srclen + 2);
        const char* src = ptr;
        const char* dst = ptr + srclen + 1;
        return fidl_link(rio, src, srclen, (zx_handle_t) off, dst, dstlen);
    }
    case ZXFIDL_GET_FLAGS: {
        uint32_t* outflags = ptr;
        return fidl_getflags(rio, outflags);
    }
    case ZXFIDL_SET_FLAGS: {
        uint32_t flags = off;
        return fidl_setflags(rio, flags);
    }
    case ZXFIDL_GET_VMO: {
        if (len != sizeof(zxrio_mmap_data_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        zxrio_mmap_data_t* data = ptr;
        zx_handle_t vmo;
        zx_status_t r = fidl_getvmo(rio, data->flags, &vmo);
        if (r != ZX_OK) {
            return r;
        }
        return vmo;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t fdio_create_fd(zx_handle_t* handles, uint32_t* types, size_t hcount,
                           int* fd_out) {
    fdio_t* io;
    zx_status_t r;
    int fd;
    zxrio_object_info_t info;
    zx_handle_t control_channel = ZX_HANDLE_INVALID;

    // Pack additional handles into |info|, if possible.
    switch (PA_HND_TYPE(types[0])) {
    case PA_FDIO_REMOTE:
        switch (hcount) {
        case 1:
            io = fdio_remote_create(handles[0], 0);
            goto bind;
        case 2:
            io = fdio_remote_create(handles[0], handles[1]);
            goto bind;
        default:
            r = ZX_ERR_INVALID_ARGS;
            goto fail;
        }
    case PA_FDIO_PIPE:
        info.tag = FDIO_PROTOCOL_PIPE;
        // Expected: Single pipe handle
        if (hcount != 1) {
            r = ZX_ERR_INVALID_ARGS;
            goto fail;
        }
        info.pipe.s = handles[0];
        break;
    case PA_FDIO_SOCKET:
        info.tag = FDIO_PROTOCOL_SOCKET_CONNECTED;
        // Expected: Single socket handle
        if (hcount != 1) {
            r = ZX_ERR_INVALID_ARGS;
            goto fail;
        }
        info.socket.s = handles[0];
        break;
    default:
        r = ZX_ERR_IO;
        goto fail;
    }

    if ((r = fdio_from_handles(control_channel, &info, &io)) != ZX_OK) {
        return r;
    }

bind:
    fd = fdio_bind_to_fd(io, -1, 0);
    if (fd < 0) {
        fdio_close(io);
        fdio_release(io);
        return ZX_ERR_BAD_STATE;
    }

    *fd_out = fd;
    return ZX_OK;
fail:
    discard_handles(handles, hcount);
    return r;
}

zx_status_t fdio_from_handles(zx_handle_t handle, zxrio_object_info_t* info,
                              fdio_t** out) {
    // All failure cases which require discard_handles set r and break
    // to the end. All other cases in which handle ownership is moved
    // on return locally.
    zx_status_t r;
    fdio_t* io;
    switch (info->tag) {
    case FDIO_PROTOCOL_DIRECTORY:
    case FDIO_PROTOCOL_SERVICE:
        if (handle == ZX_HANDLE_INVALID) {
            r = ZX_ERR_INVALID_ARGS;
            break;
        }
        io = fdio_remote_create(handle, 0);
        xprintf("rio (%x,%x) -> %p\n", handle, 0, io);
        if (io == NULL) {
            return ZX_ERR_NO_RESOURCES;
        }
        *out = io;
        return ZX_OK;
    case FDIO_PROTOCOL_FILE:
        if (info->file.e == ZX_HANDLE_INVALID) {
            io = fdio_remote_create(handle, 0);
            xprintf("rio (%x,%x) -> %p\n", handle, 0, io);
        } else {
            io = fdio_remote_create(handle, info->file.e);
            xprintf("rio (%x,%x) -> %p\n", handle, info->file.e, io);
        }
        if (io == NULL) {
            return ZX_ERR_NO_RESOURCES;
        }
        *out = io;
        return ZX_OK;
    case FDIO_PROTOCOL_DEVICE:
        if (info->device.e == ZX_HANDLE_INVALID) {
            io = fdio_remote_create(handle, 0);
            xprintf("rio (%x,%x) -> %p\n", handle, 0, io);
        } else {
            io = fdio_remote_create(handle, info->device.e);
            xprintf("rio (%x,%x) -> %p\n", handle, info->device.e, io);
        }
        if (io == NULL) {
            return ZX_ERR_NO_RESOURCES;
        }
        *out = io;
        return ZX_OK;
    case FDIO_PROTOCOL_PIPE:
        if (handle != ZX_HANDLE_INVALID) {
            r = ZX_ERR_INVALID_ARGS;
            break;
        } else if ((*out = fdio_pipe_create(info->pipe.s)) == NULL) {
            return ZX_ERR_NO_RESOURCES;
        }
        return ZX_OK;
    case FDIO_PROTOCOL_VMOFILE: {
        if (info->vmofile.v == ZX_HANDLE_INVALID) {
            r = ZX_ERR_INVALID_ARGS;
            break;
        }
        // Currently, VMO Files don't use a client-side control channel.
        zx_handle_close(handle);
        *out = fdio_vmofile_create(info->vmofile.v, info->vmofile.offset,
                                   info->vmofile.length);
        if (*out == NULL) {
            return ZX_ERR_NO_RESOURCES;
        }
        return ZX_OK;
    }
    case FDIO_PROTOCOL_SOCKET_CONNECTED:
    case FDIO_PROTOCOL_SOCKET: {
        int flags = (info->tag == FDIO_PROTOCOL_SOCKET_CONNECTED) ? IOFLAG_SOCKET_CONNECTED : 0;
        if (info->socket.s == ZX_HANDLE_INVALID) {
            r = ZX_ERR_INVALID_ARGS;
            break;
        }
        zx_handle_close(handle);
        if ((*out = fdio_socket_create(info->socket.s, flags)) == NULL) {
            return ZX_ERR_NO_RESOURCES;
        }
        return ZX_OK;
    }
    default:
        printf("fdio_from_handles: Not supported\n");
        r = ZX_ERR_NOT_SUPPORTED;
        break;
    }
    zx_handle_t extra;
    if (zxrio_object_extract_handle(info, &extra) == ZX_OK) {
        zx_handle_close(extra);
    }
    zx_handle_close(handle);
    return r;
}

zx_status_t zxrio_getobject(zx_handle_t rio_h, uint32_t op, const char* name,
                            uint32_t flags, uint32_t mode,
                            zxrio_describe_t* info, zx_handle_t* out) {
    if (name == NULL) {
        return ZX_ERR_INVALID_ARGS;
    }

    size_t len = strlen(name);
    if (len >= PATH_MAX) {
        return ZX_ERR_BAD_PATH;
    }

    if (flags & ZX_FS_FLAG_DESCRIBE) {
        return zxrio_sync_open_connection(rio_h, op, flags, mode, name, len, info, out);
    } else {
        zx_handle_t h0, h1;
        zx_status_t r;
        if ((r = zx_channel_create(0, &h0, &h1)) < 0) {
            return r;
        }
        if ((r = zxrio_connect(rio_h, h1, ZXFIDL_OPEN, flags, mode, name)) < 0) {
            zx_handle_close(h0);
            return r;
        }
        // fake up a reply message since pipelined opens don't generate one
        info->status = ZX_OK;
        info->extra.tag = FDIO_PROTOCOL_SERVICE;
        *out = h0;
        return ZX_OK;
    }
}

zx_status_t zxrio_open_handle(zx_handle_t h, const char* path, uint32_t flags,
                              uint32_t mode, fdio_t** out) {
    zx_handle_t control_channel;
    zxrio_describe_t info;
    zx_status_t r = zxrio_getobject(h, ZXFIDL_OPEN, path, flags, mode, &info, &control_channel);
    if (r < 0) {
        return r;
    }
    return fdio_from_handles(control_channel, &info.extra, out);
}

zx_status_t zxrio_open_handle_raw(zx_handle_t h, const char* path, uint32_t flags,
                                  uint32_t mode, zx_handle_t *out) {
    zx_handle_t control_channel;
    zxrio_describe_t info;
    zx_status_t r = zxrio_getobject(h, ZXFIDL_OPEN, path, flags, mode, &info, &control_channel);
    if (r < 0) {
        return r;
    }
    if (info.extra.tag == FDIO_PROTOCOL_SERVICE) {
        *out = control_channel;
        return ZX_OK;
    }
    zx_handle_t extracted;
    if (zxrio_object_extract_handle(&info.extra, &extracted) == ZX_OK) {
        zx_handle_close(extracted);
    }
    return ZX_ERR_WRONG_TYPE;
}

zx_status_t zxrio_open(fdio_t* io, const char* path, uint32_t flags, uint32_t mode, fdio_t** out) {
    zxrio_t* rio = (void*)io;
    return zxrio_open_handle(rio->h, path, flags, mode, out);
}

static zx_status_t zxrio_clone(fdio_t* io, zx_handle_t* handles, uint32_t* types) {
    zxrio_t* rio = (void*)io;
    zx_handle_t h;
    zxrio_describe_t info;
    zx_status_t r = zxrio_getobject(rio->h, ZXFIDL_CLONE, "", ZX_FS_FLAG_DESCRIBE, 0, &info, &h);
    if (r < 0) {
        return r;
    }
    handles[0] = h;
    types[0] = PA_FDIO_REMOTE;
    if (zxrio_object_extract_handle(&info.extra, &handles[1]) == ZX_OK) {
        types[1] = PA_FDIO_REMOTE;
        return 2;
    }
    return 1;
}

static zx_status_t zxrio_unwrap(fdio_t* io, zx_handle_t* handles, uint32_t* types) {
    zxrio_t* rio = (void*)io;
    LOG(1, "fdio: zxrio_unwrap(%p,...)\n");
    zx_status_t r;
    handles[0] = rio->h;
    types[0] = PA_FDIO_REMOTE;
    if (rio->h2 != 0) {
        handles[1] = rio->h2;
        types[1] = PA_FDIO_REMOTE;
        r = 2;
    } else {
        r = 1;
    }
    return r;
}

static void zxrio_wait_begin(fdio_t* io, uint32_t events, zx_handle_t* handle, zx_signals_t* _signals) {
    zxrio_t* rio = (void*)io;
    *handle = rio->h2;

    zx_signals_t signals = 0;
    // Manually add signals that don't fit within POLL_MASK
    if (events & POLLRDHUP) {
        signals |= ZX_CHANNEL_PEER_CLOSED;
    }

    // POLLERR is always detected
    *_signals = (((POLLERR | events) & POLL_MASK) << POLL_SHIFT) | signals;
}

static void zxrio_wait_end(fdio_t* io, zx_signals_t signals, uint32_t* _events) {
    // Manually add events that don't fit within POLL_MASK
    uint32_t events = 0;
    if (signals & ZX_CHANNEL_PEER_CLOSED) {
        events |= POLLRDHUP;
    }
    *_events = ((signals >> POLL_SHIFT) & POLL_MASK) | events;
}

static zx_status_t zxrio_get_vmo(fdio_t* io, int flags, zx_handle_t* out) {
    zx_handle_t vmo;
    zxrio_t* rio = (zxrio_t*)io;
    zx_status_t r = fidl_getvmo(rio, flags, &vmo);
    if (r != ZX_OK) {
        return r;
    }
    *out = vmo;
    return ZX_OK;
}

static fdio_ops_t zx_remote_ops = {
    .read = zxrio_read,
    .read_at = zxrio_read_at,
    .write = zxrio_write,
    .write_at = zxrio_write_at,
    .recvfrom = fdio_default_recvfrom,
    .sendto = fdio_default_sendto,
    .recvmsg = fdio_default_recvmsg,
    .sendmsg = fdio_default_sendmsg,
    .misc = zxrio_misc,
    .seek = zxrio_seek,
    .close = zxrio_close,
    .open = zxrio_open,
    .clone = zxrio_clone,
    .ioctl = zxrio_ioctl,
    .wait_begin = zxrio_wait_begin,
    .wait_end = zxrio_wait_end,
    .unwrap = zxrio_unwrap,
    .shutdown = fdio_default_shutdown,
    .posix_ioctl = fdio_default_posix_ioctl,
    .get_vmo = zxrio_get_vmo,
};

fdio_t* fdio_remote_create(zx_handle_t h, zx_handle_t e) {
    zxrio_t* rio = fdio_alloc(sizeof(*rio));
    if (rio == NULL) {
        zx_handle_close(h);
        zx_handle_close(e);
        return NULL;
    }
    rio->io.ops = &zx_remote_ops;
    rio->io.magic = FDIO_MAGIC;
    atomic_init(&rio->io.refcount, 1);
    rio->h = h;
    rio->h2 = e;
    return &rio->io;
}
