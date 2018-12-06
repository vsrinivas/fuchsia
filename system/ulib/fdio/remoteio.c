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
#include <lib/fdio/util.h>
#include <lib/fdio/vfs.h>

#include "private-remoteio.h"

#define ZXDEBUG 0

// POLL_MASK and POLL_SHIFT intend to convert the lower five POLL events into
// ZX_USER_SIGNALs and vice-versa. Other events need to be manually converted to
// a zx_signals_t, if they are desired.
#define POLL_SHIFT  24
#define POLL_MASK   0x1F

static_assert(FDIO_CHUNK_SIZE >= PATH_MAX,
              "FDIO_CHUNK_SIZE must be large enough to contain paths");

static_assert(fuchsia_io_VMO_FLAG_READ == ZX_VM_PERM_READ,
              "Vmar / Vmo flags should be aligned");
static_assert(fuchsia_io_VMO_FLAG_WRITE == ZX_VM_PERM_WRITE,
              "Vmar / Vmo flags should be aligned");
static_assert(fuchsia_io_VMO_FLAG_EXEC == ZX_VM_PERM_EXECUTE,
              "Vmar / Vmo flags should be aligned");

static_assert(ZX_USER_SIGNAL_0 == (1 << POLL_SHIFT), "");
static_assert((POLLIN << POLL_SHIFT) == DEVICE_SIGNAL_READABLE, "");
static_assert((POLLPRI << POLL_SHIFT) == DEVICE_SIGNAL_OOB, "");
static_assert((POLLOUT << POLL_SHIFT) == DEVICE_SIGNAL_WRITABLE, "");
static_assert((POLLERR << POLL_SHIFT) == DEVICE_SIGNAL_ERROR, "");
static_assert((POLLHUP << POLL_SHIFT) == DEVICE_SIGNAL_HANGUP, "");

// Acquire the additional handle from |info|.
//
// Returns |ZX_OK| if a handle was returned.
// Returns |ZX_ERR_NOT_FOUND| if no handle can be returned.
static zx_status_t zxrio_object_extract_handle(const fuchsia_io_NodeInfo* info,
                                               zx_handle_t* out) {
    switch (info->tag) {
    case fuchsia_io_NodeInfoTag_file:
        if (info->file.event != ZX_HANDLE_INVALID) {
            *out = info->file.event;
            return ZX_OK;
        }
        break;
    case fuchsia_io_NodeInfoTag_pipe:
        if (info->pipe.socket != ZX_HANDLE_INVALID) {
            *out = info->pipe.socket;
            return ZX_OK;
        }
        break;
    case fuchsia_io_NodeInfoTag_vmofile:
        if (info->vmofile.vmo != ZX_HANDLE_INVALID) {
            *out = info->vmofile.vmo;
            return ZX_OK;
        }
        break;
    case fuchsia_io_NodeInfoTag_device:
        if (info->device.event != ZX_HANDLE_INVALID) {
            *out = info->device.event;
            return ZX_OK;
        }
        break;
    }
    return ZX_ERR_NOT_FOUND;
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
    case fuchsia_io_NodeCloneOrdinal:
        r = fuchsia_io_NodeClone(svc, flags, cnxn);
        break;
    case fuchsia_io_DirectoryOpenOrdinal:
        r = fuchsia_io_DirectoryOpen(svc, flags, mode, name, len, cnxn);
        break;
    default:
        zx_handle_close(cnxn);
        r = ZX_ERR_NOT_SUPPORTED;
    }
    return r;
}

// A one-way message which may be emitted by the server without an
// accompanying request. Optionally used as a part of the Open handshake.
typedef struct {
    fuchsia_io_NodeOnOpenEvent primary;
    fuchsia_io_NodeInfo extra;
} fdio_on_open_msg_t;

// Takes ownership of the optional |extra_handle|.
//
// Decodes the handle into |info|, if it exists and should
// be decoded.
static zx_status_t zxrio_decode_describe_handle(fdio_on_open_msg_t* info,
                                                zx_handle_t extra_handle) {
    bool have_handle = (extra_handle != ZX_HANDLE_INVALID);
    bool want_handle = false;
    zx_handle_t* handle_target = NULL;

    switch (info->extra.tag) {
    // Case: No extra handles expected
    case fuchsia_io_NodeInfoTag_service:
    case fuchsia_io_NodeInfoTag_directory:
        break;
    // Case: Extra handles optional
    case fuchsia_io_NodeInfoTag_file:
        handle_target = &info->extra.file.event;
        goto handle_optional;
    case fuchsia_io_NodeInfoTag_device:
        handle_target = &info->extra.device.event;
        goto handle_optional;
handle_optional:
        want_handle = *handle_target == FIDL_HANDLE_PRESENT;
        break;
    // Case: Extra handles required
    case fuchsia_io_NodeInfoTag_pipe:
        handle_target = &info->extra.pipe.socket;
        goto handle_required;
    case fuchsia_io_NodeInfoTag_vmofile:
        handle_target = &info->extra.vmofile.vmo;
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

// Wait/Read from a new client connection, with the expectation of
// acquiring an Open response.
//
// Shared implementation between RemoteIO and FIDL, since the response
// message is aligned.
//
// Does not close |h|, even on error.
static zx_status_t zxrio_process_open_response(zx_handle_t h, fdio_on_open_msg_t* info) {
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
    if (dsize < sizeof(fuchsia_io_NodeOnOpenEvent) ||
        info->primary.hdr.ordinal != fuchsia_io_NodeOnOpenOrdinal) {
        r = ZX_ERR_IO;
    } else {
        r = info->primary.s;
    }

    if (dsize != sizeof(fdio_on_open_msg_t)) {
        r = (r != ZX_OK) ? r : ZX_ERR_IO;
    }

    if (r != ZX_OK) {
        if (extra_handle != ZX_HANDLE_INVALID) {
            zx_handle_close(extra_handle);
        }
        return r;
    }

    // Confirm that the objects "fdio_on_open_msg_t" and "fuchsia_io_NodeOnOpenEvent"
    // are aligned enough to be compatible.
    //
    // This is somewhat complicated by the fact that the "fuchsia_io_NodeOnOpenEvent"
    // object has an optional "fuchsia_io_NodeInfo" secondary which exists immediately
    // following the struct.
    static_assert(__builtin_offsetof(fdio_on_open_msg_t, extra) ==
                  FIDL_ALIGN(sizeof(fuchsia_io_NodeOnOpenEvent)),
                  "RIO Description message doesn't align with FIDL response secondary");
    // Connection::NodeDescribe also relies on these static_asserts.
    // fidl_describe also relies on these static_asserts.

    return zxrio_decode_describe_handle(info, extra_handle);
}

__EXPORT
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

__EXPORT
zx_status_t fdio_service_connect_at(zx_handle_t dir, const char* path, zx_handle_t h) {
    if (path == NULL) {
        zx_handle_close(h);
        return ZX_ERR_INVALID_ARGS;
    }
    if (dir == ZX_HANDLE_INVALID) {
        zx_handle_close(h);
        return ZX_ERR_UNAVAILABLE;
    }
    return zxrio_connect(dir, h, fuchsia_io_DirectoryOpenOrdinal, ZX_FS_RIGHT_READABLE |
                         ZX_FS_RIGHT_WRITABLE, 0755, path);
}

__EXPORT
zx_status_t fdio_open(const char* path, uint32_t flags, zx_handle_t h) {
    if (path == NULL) {
        zx_handle_close(h);
        return ZX_ERR_INVALID_ARGS;
    }
    // Otherwise attempt to connect through the root namespace
    if (fdio_root_ns != NULL) {
        return fdio_ns_connect(fdio_root_ns, path, flags, h);
    }
    // Otherwise we fail
    zx_handle_close(h);
    return ZX_ERR_NOT_FOUND;
}

__EXPORT
zx_status_t fdio_open_at(zx_handle_t dir, const char* path, uint32_t flags, zx_handle_t h) {
    if (path == NULL) {
        zx_handle_close(h);
        return ZX_ERR_INVALID_ARGS;
    }
    if (dir == ZX_HANDLE_INVALID) {
        zx_handle_close(h);
        return ZX_ERR_UNAVAILABLE;
    }
    return zxrio_connect(dir, h, fuchsia_io_DirectoryOpenOrdinal, flags, 0755, path);
}

__EXPORT
zx_handle_t fdio_service_clone(zx_handle_t svc) {
    zx_handle_t cli, srv;
    zx_status_t r;
    if (svc == ZX_HANDLE_INVALID) {
        return ZX_HANDLE_INVALID;
    }
    if ((r = zx_channel_create(0, &cli, &srv)) < 0) {
        return ZX_HANDLE_INVALID;
    }
    if ((r = zxrio_connect(svc, srv, fuchsia_io_NodeCloneOrdinal, ZX_FS_RIGHT_READABLE |
                           ZX_FS_RIGHT_WRITABLE, 0755, "")) < 0) {
        zx_handle_close(cli);
        return ZX_HANDLE_INVALID;
    }
    return cli;
}

__EXPORT
zx_status_t fdio_service_clone_to(zx_handle_t svc, zx_handle_t srv) {
    if (srv == ZX_HANDLE_INVALID) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (svc == ZX_HANDLE_INVALID) {
        zx_handle_close(srv);
        return ZX_ERR_INVALID_ARGS;
    }
    return zxrio_connect(svc, srv, fuchsia_io_NodeCloneOrdinal, ZX_FS_RIGHT_READABLE |
                         ZX_FS_RIGHT_WRITABLE, 0755, "");
}

zx_status_t fdio_from_channel(zx_handle_t channel, fdio_t** out_io) {
    fuchsia_io_NodeInfo info;
    memset(&info, 0, sizeof(info));
    zx_status_t status = fuchsia_io_NodeDescribe(channel, &info);
    if (status != ZX_OK) {
        zx_handle_close(channel);
        return status;
    }

    zx_handle_t event = ZX_HANDLE_INVALID;
    switch (info.tag) {
    case fuchsia_io_NodeInfoTag_file:
        event = info.file.event;
        break;
    case fuchsia_io_NodeInfoTag_device:
        event = info.device.event;
        break;
    case fuchsia_io_NodeInfoTag_vmofile: {
        uint64_t seek = 0u;
        zx_status_t io_status = fuchsia_io_FileSeek(
            channel, 0, fuchsia_io_SeekOrigin_START, &status, &seek);
        if (io_status != ZX_OK) {
            status = io_status;
        }
        if (status != ZX_OK) {
            zx_handle_close(channel);
            zx_handle_close(info.vmofile.vmo);
            return status;
        }
        *out_io = fdio_vmofile_create(channel, info.vmofile.vmo,
                                      info.vmofile.offset, info.vmofile.length,
                                      seek);
        return ZX_OK;
    }
    default:
        event = ZX_HANDLE_INVALID;
        break;
    }

    *out_io = fdio_remote_create(channel, event);
    return ZX_OK;
}

zx_status_t fdio_from_socket(zx_handle_t socket, fdio_t** out_io) {
    zx_info_socket_t info;
    memset(&info, 0, sizeof(info));
    zx_status_t status = zx_object_get_info(socket, ZX_INFO_SOCKET, &info, sizeof(info), NULL, NULL);
    if (status != ZX_OK) {
        zx_handle_close(socket);
        return status;
    }
    fdio_t* io = NULL;
    if ((info.options & ZX_SOCKET_HAS_CONTROL) != 0) {
        // If the socket has a control plane, then the socket is either
        // a stream or a datagram socket.
        if ((info.options & ZX_SOCKET_DATAGRAM) != 0) {
            io = fdio_socket_create_datagram(socket, IOFLAG_SOCKET_CONNECTED);
        } else {
            io = fdio_socket_create_stream(socket, IOFLAG_SOCKET_CONNECTED);
        }
    } else {
        // Without a control plane, the socket is a pipe.
        io = fdio_pipe_create(socket);
    }
    if (!io) {
        return ZX_ERR_NO_RESOURCES;
    }
    *out_io = io;
    return ZX_OK;
}

// Create a fdio (if possible) from handles and info.
//
// The Control channel is provided in |handle|, and auxiliary
// handles may be provided in the |info| object.
//
// This function always takes control of all handles.
// They are transferred into the |out| object on success,
// or closed on failure.
static zx_status_t fdio_from_handles(zx_handle_t handle, fuchsia_io_NodeInfo* info,
                                     fdio_t** out) {
    // All failure cases which discard handles set r and break
    // to the end. All other cases in which handle ownership is moved
    // on return locally.
    zx_status_t r;
    fdio_t* io;
    switch (info->tag) {
    case fuchsia_io_NodeInfoTag_directory:
    case fuchsia_io_NodeInfoTag_service:
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
    case fuchsia_io_NodeInfoTag_file:
        if (info->file.event == ZX_HANDLE_INVALID) {
            io = fdio_remote_create(handle, 0);
            xprintf("rio (%x,%x) -> %p\n", handle, 0, io);
        } else {
            io = fdio_remote_create(handle, info->file.event);
            xprintf("rio (%x,%x) -> %p\n", handle, info->file.event, io);
        }
        if (io == NULL) {
            return ZX_ERR_NO_RESOURCES;
        }
        *out = io;
        return ZX_OK;
    case fuchsia_io_NodeInfoTag_device:
        if (info->device.event == ZX_HANDLE_INVALID) {
            io = fdio_remote_create(handle, 0);
            xprintf("rio (%x,%x) -> %p\n", handle, 0, io);
        } else {
            io = fdio_remote_create(handle, info->device.event);
            xprintf("rio (%x,%x) -> %p\n", handle, info->device.event, io);
        }
        if (io == NULL) {
            return ZX_ERR_NO_RESOURCES;
        }
        *out = io;
        return ZX_OK;
    case fuchsia_io_NodeInfoTag_vmofile: {
        if (info->vmofile.vmo == ZX_HANDLE_INVALID) {
            r = ZX_ERR_INVALID_ARGS;
            break;
        }
        *out = fdio_vmofile_create(handle, info->vmofile.vmo, info->vmofile.offset,
                                   info->vmofile.length, 0u);
        if (*out == NULL) {
            return ZX_ERR_NO_RESOURCES;
        }
        return ZX_OK;
    }
    case fuchsia_io_NodeInfoTag_pipe: {
        if (info->pipe.socket == ZX_HANDLE_INVALID) {
            r = ZX_ERR_INVALID_ARGS;
            break;
        }
        zx_handle_close(handle);
        return fdio_from_socket(info->pipe.socket, out);
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

__EXPORT
zx_status_t fdio_create_fd(zx_handle_t* handles, uint32_t* types, size_t hcount,
                           int* fd_out) {
    fdio_t* io;
    zx_status_t r;
    int fd;
    fuchsia_io_NodeInfo info;

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
    case PA_FDIO_SOCKET:
        info.tag = fuchsia_io_NodeInfoTag_pipe;
        // Expected: Single socket handle
        if (hcount != 1) {
            r = ZX_ERR_INVALID_ARGS;
            goto fail;
        }
        info.pipe.socket = handles[0];
        break;
    default:
        r = ZX_ERR_IO;
        goto fail;
    }

    if ((r = fdio_from_handles(ZX_HANDLE_INVALID, &info, &io)) != ZX_OK) {
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
    zx_handle_close_many(handles, hcount);
    return r;
}

// Synchronously (non-pipelined) open an object
// The svc handle is only used to send a message
static zx_status_t zxrio_sync_open_connection(zx_handle_t svc, uint32_t op,
                                              uint32_t flags, uint32_t mode,
                                              const char* path, size_t pathlen,
                                              fdio_on_open_msg_t* info,
                                              zx_handle_t* out) {
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
    case fuchsia_io_NodeCloneOrdinal:
        r = fuchsia_io_NodeClone(svc, flags, cnxn);
        break;
    case fuchsia_io_DirectoryOpenOrdinal:
        r = fuchsia_io_DirectoryOpen(svc, flags, mode, path, pathlen, cnxn);
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

// Acquires a new connection to an object.
//
// Returns a description of the opened object in |info|, and
// the control channel to the object in |out|.
//
// |info| may contain an additional handle.
static zx_status_t zxrio_getobject(zx_handle_t rio_h, uint32_t op, const char* name,
                                   uint32_t flags, uint32_t mode,
                                   fdio_on_open_msg_t* info, zx_handle_t* out) {
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
        if ((r = zxrio_connect(rio_h, h1, op, flags, mode, name)) < 0) {
            zx_handle_close(h0);
            return r;
        }
        // fake up a reply message since pipelined opens don't generate one
        info->primary.s = ZX_OK;
        info->extra.tag = fuchsia_io_NodeInfoTag_service;
        *out = h0;
        return ZX_OK;
    }
}

zx_status_t zxrio_open_handle(zx_handle_t h, const char* path, uint32_t flags,
                              uint32_t mode, fdio_t** out) {
    zx_handle_t control_channel = ZX_HANDLE_INVALID;
    fdio_on_open_msg_t info;
    zx_status_t r = zxrio_getobject(h, fuchsia_io_DirectoryOpenOrdinal, path, flags, mode, &info, &control_channel);
    if (r < 0) {
        return r;
    }
    return fdio_from_handles(control_channel, &info.extra, out);
}
