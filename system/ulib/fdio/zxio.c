// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxio/inception.h>
#include <lib/zxio/zxio.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include "private-fidl.h"
#include "private-remoteio.h"
#include "private.h"

// Initial memory layout for types that bridge between |fdio_t| and |zxio_t|.
//
// Every |fdio_t| implementation starts with an embedded |fdio_t|, which the
// callers use to find the fdio |ops| table. There are several |fdio_t|
// implementations that use zxio as a backed. All of them have an initial memory
// layout that matches this structure. Defining this structure lets us define
// most of the fdio ops that use the zxio backend in a generic way.
//
// Will be removed once the transition to the zxio backend is complete.
typedef struct fdio_zxio {
    fdio_t io;
    zxio_t zio;
} fdio_zxio_t;

static inline zxio_t* fdio_get_zxio(fdio_t* io) {
    fdio_zxio_t* wrapper = (fdio_zxio_t*)io;
    return &wrapper->zio;
}

static zx_status_t fdio_zxio_close(fdio_t* io) {
    zxio_t* z = fdio_get_zxio(io);
    return zxio_close(z);
}

static void fdio_zxio_wait_begin(fdio_t* io, uint32_t events,
                                 zx_handle_t* out_handle,
                                 zx_signals_t* out_signals) {
    zxio_t* z = fdio_get_zxio(io);
    zxio_signals_t signals = ZXIO_SIGNAL_NONE;
    if (events & POLLIN) {
        signals |= ZXIO_READABLE | ZXIO_READ_DISABLED;
    }
    if (events & POLLOUT) {
        signals |= ZXIO_WRITABLE | ZXIO_WRITE_DISABLED;
    }
    if (events & POLLRDHUP) {
        signals |= ZXIO_READ_DISABLED;
    }
    zxio_wait_begin(z, signals, out_handle, out_signals);
}

static void fdio_zxio_wait_end(fdio_t* io, zx_signals_t signals,
                               uint32_t* out_events) {
    zxio_t* z = fdio_get_zxio(io);
    zxio_signals_t zxio_signals = ZXIO_SIGNAL_NONE;
    zxio_wait_end(z, signals, &zxio_signals);

    uint32_t events = 0;
    if (zxio_signals & (ZXIO_READABLE | ZXIO_READ_DISABLED)) {
        events |= POLLIN;
    }
    if (zxio_signals & (ZXIO_WRITABLE | ZXIO_WRITE_DISABLED)) {
        events |= POLLOUT;
    }
    if (zxio_signals & ZXIO_READ_DISABLED) {
        events |= POLLRDHUP;
    }
    *out_events = events;
}

static zx_status_t fdio_zxio_sync(fdio_t* io) {
    zxio_t* z = fdio_get_zxio(io);
    return zxio_sync(z);
}

static zx_status_t fdio_zxio_get_attr(fdio_t* io, vnattr_t* out) {
    zxio_t* z = fdio_get_zxio(io);
    zxio_node_attr_t attr;
    zx_status_t status = zxio_attr_get(z, &attr);
    if (status != ZX_OK) {
        return status;
    }

    // Translate zxio_node_attr_t --> vnattr
    out->mode = attr.mode;
    out->inode = attr.id;
    out->size = attr.content_size;
    out->blksize = VNATTR_BLKSIZE;
    out->blkcount = attr.storage_size / VNATTR_BLKSIZE;
    out->nlink = attr.link_count;
    out->create_time = attr.creation_time;
    out->modify_time = attr.modification_time;

    return ZX_OK;
}

static zx_status_t fdio_zxio_set_attr(fdio_t* io, const vnattr_t* vnattr) {
    zxio_t* z = fdio_get_zxio(io);
    uint32_t flags = vnattr->valid;
    zxio_node_attr_t attr;
    memset(&attr, 0, sizeof(attr));
    attr.creation_time = vnattr->create_time;
    attr.modification_time = vnattr->modify_time;
    return zxio_attr_set(z, flags, &attr);
}

static ssize_t fdio_zxio_read(fdio_t* io, void* data, size_t len) {
    zxio_t* z = fdio_get_zxio(io);
    size_t actual = 0;
    zx_status_t status = zxio_read(z, data, len, &actual);
    return status != ZX_OK ? status : (ssize_t)actual;
}

static ssize_t fdio_zxio_read_at(fdio_t* io, void* data, size_t len, off_t at) {
    zxio_t* z = fdio_get_zxio(io);
    size_t actual = 0;
    zx_status_t status = zxio_read_at(z, at, data, len, &actual);
    return status != ZX_OK ? status : (ssize_t)actual;
}

static ssize_t fdio_zxio_write(fdio_t* io, const void* data, size_t len) {
    zxio_t* z = fdio_get_zxio(io);
    size_t actual = 0;
    zx_status_t status = zxio_write(z, data, len, &actual);
    return status != ZX_OK ? status : (ssize_t)actual;
}

static ssize_t fdio_zxio_write_at(fdio_t* io, const void* data, size_t len, off_t at) {
    zxio_t* z = fdio_get_zxio(io);
    size_t actual = 0;
    zx_status_t status = zxio_write_at(z, at, data, len, &actual);
    return status != ZX_OK ? status : (ssize_t)actual;
}

static_assert(SEEK_SET == fuchsia_io_SeekOrigin_START, "");
static_assert(SEEK_CUR == fuchsia_io_SeekOrigin_CURRENT, "");
static_assert(SEEK_END == fuchsia_io_SeekOrigin_END, "");

static off_t fdio_zxio_seek(fdio_t* io, off_t offset, int whence) {
    zxio_t* z = fdio_get_zxio(io);
    size_t result = 0u;
    zx_status_t status = zxio_seek(z, offset, whence, &result);
    return status != ZX_OK ? status : (ssize_t)result;
}

static zx_status_t fdio_zxio_truncate(fdio_t* io, off_t off) {
    zxio_t* z = fdio_get_zxio(io);
    return zxio_truncate(z, off);
}

static zx_status_t fdio_zxio_get_flags(fdio_t* io, uint32_t* out_flags) {
    zxio_t* z = fdio_get_zxio(io);
    return zxio_flags_get(z, out_flags);
}

static zx_status_t fdio_zxio_set_flags(fdio_t* io, uint32_t flags) {
    zxio_t* z = fdio_get_zxio(io);
    return zxio_flags_set(z, flags);
}

// Remote ----------------------------------------------------------------------

static_assert(offsetof(fdio_zxio_t, zio) == offsetof(fdio_zxio_remote_t, remote.io),
              "fdio_zxio_remote_t layout must match fdio_zxio_t");

// POLL_MASK and POLL_SHIFT intend to convert the lower five POLL events into
// ZX_USER_SIGNALs and vice-versa. Other events need to be manually converted to
// a zx_signals_t, if they are desired.
#define POLL_SHIFT  24
#define POLL_MASK   0x1F

static inline zxio_remote_t* fdio_get_zxio_remote(fdio_t* io) {
    fdio_zxio_remote_t* wrapper = (fdio_zxio_remote_t*)io;
    return &wrapper->remote;
}

static zx_status_t fdio_zxio_remote_open(fdio_t* io, const char* path,
                                         uint32_t flags, uint32_t mode,
                                         fdio_t** out) {
    zxio_remote_t* rio = fdio_get_zxio_remote(io);
    return zxrio_open_handle(rio->control, path, flags, mode, out);
}

static zx_status_t fdio_zxio_remote_clone(fdio_t* io, zx_handle_t* handles, uint32_t* types) {
    zxio_t* z = fdio_get_zxio(io);
    zx_handle_t local, remote;
    zx_status_t status = zx_channel_create(0, &local, &remote);
    if (status != ZX_OK) {
        return status;
    }
    uint32_t flags = fuchsia_io_OPEN_RIGHT_READABLE | fuchsia_io_OPEN_RIGHT_WRITABLE;
    status = zxio_clone_async(z, flags, remote);
    if (status != ZX_OK) {
        zx_handle_close(local);
        return status;
    }
    handles[0] = local;
    types[0] = PA_FDIO_REMOTE;
    return 1;
}

static ssize_t fdio_zxio_remote_ioctl(fdio_t* io, uint32_t op, const void* in_buf,
                                      size_t in_len, void* out_buf, size_t out_len) {
    zxio_remote_t* rio = fdio_get_zxio_remote(io);
    if (in_len > FDIO_IOCTL_MAX_INPUT || out_len > FDIO_CHUNK_SIZE) {
        return ZX_ERR_INVALID_ARGS;
    }
    size_t actual = 0u;
    zx_status_t status = fidl_ioctl(rio->control, op, in_buf, in_len, out_buf, out_len, &actual);
    if (status != ZX_OK) {
        return status;
    }
    return actual;
}

static void fdio_zxio_remote_wait_begin(fdio_t* io, uint32_t events,
                                        zx_handle_t* handle, zx_signals_t* _signals) {
    zxio_remote_t* rio = fdio_get_zxio_remote(io);
    *handle = rio->event;

    zx_signals_t signals = 0;
    // Manually add signals that don't fit within POLL_MASK
    if (events & POLLRDHUP) {
        signals |= ZX_CHANNEL_PEER_CLOSED;
    }

    // POLLERR is always detected
    *_signals = (((POLLERR | events) & POLL_MASK) << POLL_SHIFT) | signals;
}

static void fdio_zxio_remote_wait_end(fdio_t* io, zx_signals_t signals, uint32_t* _events) {
    // Manually add events that don't fit within POLL_MASK
    uint32_t events = 0;
    if (signals & ZX_CHANNEL_PEER_CLOSED) {
        events |= POLLRDHUP;
    }
    *_events = ((signals >> POLL_SHIFT) & POLL_MASK) | events;
}

static zx_status_t fdio_zxio_remote_unwrap(fdio_t* io, zx_handle_t* handles, uint32_t* types) {
    zxio_t* z = fdio_get_zxio(io);
    zx_handle_t handle = ZX_HANDLE_INVALID;
    zx_status_t status = zxio_release(z, &handle);
    if (status != ZX_OK) {
        return status;
    }
    handles[0] = handle;
    types[0] = PA_FDIO_REMOTE;
    return 1;
}

static zx_status_t fdio_zxio_remote_get_vmo(fdio_t* io, int flags, zx_handle_t* out_vmo) {
    zxio_remote_t* rio = fdio_get_zxio_remote(io);
    zx_handle_t vmo = ZX_HANDLE_INVALID;
    zx_status_t io_status, status;
    io_status = fuchsia_io_FileGetVmo(rio->control, flags, &status, &vmo);
    if (io_status != ZX_OK) {
        return io_status;
    }
    if (status != ZX_OK) {
        return status;
    }
    if (vmo == ZX_HANDLE_INVALID) {
        return ZX_ERR_IO;
    }
    *out_vmo = vmo;
    return ZX_OK;
}

static zx_status_t fdio_zxio_remote_get_token(fdio_t* io, zx_handle_t* out_token) {
    zxio_remote_t* rio = fdio_get_zxio_remote(io);
    zx_status_t io_status, status;
    io_status = fuchsia_io_DirectoryGetToken(rio->control, &status, out_token);
    return io_status != ZX_OK ? io_status : status;
}

static zx_status_t fdio_zxio_remote_readdir(fdio_t* io, void* ptr, size_t max, size_t* out_actual) {
    zxio_remote_t* rio = fdio_get_zxio_remote(io);
    size_t actual = 0u;
    zx_status_t io_status, status;
    io_status = fuchsia_io_DirectoryReadDirents(rio->control, max, &status, ptr,
                                                max, &actual);
    if (io_status != ZX_OK) {
        return io_status;
    }
    if (status != ZX_OK) {
        return status;
    }
    if (actual > max) {
        return ZX_ERR_IO;
    }
    *out_actual = actual;
    return status;
}

static zx_status_t fdio_zxio_remote_rewind(fdio_t* io) {
    zxio_remote_t* rio = fdio_get_zxio_remote(io);
    zx_status_t io_status, status;
    io_status = fuchsia_io_DirectoryRewind(rio->control, &status);
    return io_status != ZX_OK ? io_status : status;
}

static zx_status_t fdio_zxio_remote_unlink(fdio_t* io, const char* path, size_t len) {
    zxio_remote_t* rio = fdio_get_zxio_remote(io);
    zx_status_t io_status, status;
    io_status = fuchsia_io_DirectoryUnlink(rio->control, path, len, &status);
    return io_status != ZX_OK ? io_status : status;
}

static zx_status_t fdio_zxio_remote_rename(fdio_t* io, const char* src, size_t srclen,
                                           zx_handle_t dst_token, const char* dst, size_t dstlen) {
    zxio_remote_t* rio = fdio_get_zxio_remote(io);
    zx_status_t io_status, status;
    io_status = fuchsia_io_DirectoryRename(rio->control, src, srclen, dst_token,
                                           dst, dstlen, &status);
    return io_status != ZX_OK ? io_status : status;
}

static zx_status_t fdio_zxio_remote_link(fdio_t* io, const char* src, size_t srclen,
                                         zx_handle_t dst_token, const char* dst, size_t dstlen) {
    zxio_remote_t* rio = fdio_get_zxio_remote(io);
    zx_status_t io_status, status;
    io_status = fuchsia_io_DirectoryLink(rio->control, src, srclen, dst_token,
                                         dst, dstlen, &status);
    return io_status != ZX_OK ? io_status : status;
}

fdio_ops_t fdio_zxio_remote_ops = {
    .read = fdio_zxio_read,
    .read_at = fdio_zxio_read_at,
    .write = fdio_zxio_write,
    .write_at = fdio_zxio_write_at,
    .seek = fdio_zxio_seek,
    .misc = fdio_default_misc,
    .close = fdio_zxio_close,
    .open = fdio_zxio_remote_open,
    .clone = fdio_zxio_remote_clone,
    .ioctl = fdio_zxio_remote_ioctl,
    .wait_begin = fdio_zxio_remote_wait_begin,
    .wait_end = fdio_zxio_remote_wait_end,
    .unwrap = fdio_zxio_remote_unwrap,
    .posix_ioctl = fdio_default_posix_ioctl,
    .get_vmo = fdio_zxio_remote_get_vmo,
    .get_token = fdio_zxio_remote_get_token,
    .get_attr = fdio_zxio_get_attr,
    .set_attr = fdio_zxio_set_attr,
    .sync = fdio_zxio_sync,
    .readdir = fdio_zxio_remote_readdir,
    .rewind = fdio_zxio_remote_rewind,
    .unlink = fdio_zxio_remote_unlink,
    .truncate = fdio_zxio_truncate,
    .rename = fdio_zxio_remote_rename,
    .link = fdio_zxio_remote_link,
    .get_flags = fdio_zxio_get_flags,
    .set_flags = fdio_zxio_set_flags,
    .recvfrom = fdio_default_recvfrom,
    .sendto = fdio_default_sendto,
    .recvmsg = fdio_default_recvmsg,
    .sendmsg = fdio_default_sendmsg,
    .shutdown = fdio_default_shutdown,
};

fdio_t* fdio_zxio_create_remote(zx_handle_t control, zx_handle_t event) {
    fdio_zxio_remote_t* fv = fdio_alloc(sizeof(fdio_zxio_remote_t));
    if (fv == NULL) {
        zx_handle_close(control);
        zx_handle_close(event);
        return NULL;
    }
    fv->io.ops = &fdio_zxio_remote_ops;
    fv->io.magic = FDIO_MAGIC;
    atomic_init(&fv->io.refcount, 1);
    zx_status_t status = zxio_remote_init(&fv->remote, control, event);
    if (status != ZX_OK) {
        return NULL;
    }
    return &fv->io;
}

// Pipe ------------------------------------------------------------------------

// Implements the |fdio_t| contract using |zxio_pipe_t|.
//
// Has an ops table that translates fdio ops into zxio ops. Some of the fdio ops
// require using the underlying handles in the |zxio_pipe_t|, which is why
// this object needs to use |zxio_pipe_t| directly.
//
// Will be removed once the transition to the zxio backend is complete.
typedef struct fdio_zxio_pipe {
    fdio_t io;
    zxio_pipe_t pipe;
} fdio_zxio_pipe_t;

static_assert(offsetof(fdio_zxio_t, zio) == offsetof(fdio_zxio_pipe_t, pipe.io),
              "fdio_zxio_pipe_t layout must match fdio_zxio_t");

static zx_status_t read_blocking(zxio_t* io, void* buffer, size_t capacity,
                                 size_t* out_actual) {
    for (;;) {
        zx_status_t status = zxio_read(io, buffer, capacity, out_actual);
        if (status != ZX_ERR_SHOULD_WAIT) {
            return status;
        }
        zxio_signals_t observed = ZXIO_SIGNAL_NONE;
        status = zxio_wait_one(io, ZXIO_READABLE | ZXIO_READ_DISABLED,
                               ZX_TIME_INFINITE, &observed);
        if (status != ZX_OK) {
            return status;
        }
    }
}

static zx_status_t write_blocking(zxio_t* io, const void* buffer,
                                  size_t capacity, size_t* out_actual) {
    for (;;) {
        zx_status_t status = zxio_write(io, buffer, capacity, out_actual);
        if (status != ZX_ERR_SHOULD_WAIT) {
            return status;
        }
        zxio_signals_t observed = ZXIO_SIGNAL_NONE;
        status = zxio_wait_one(io, ZXIO_WRITABLE | ZXIO_WRITE_DISABLED,
                               ZX_TIME_INFINITE, &observed);
        if (status != ZX_OK) {
            return status;
        }
    }
}

static ssize_t read_internal(zxio_t* io, bool blocking, void* data, size_t len) {
    size_t actual = 0u;
    zx_status_t status = ZX_OK;
    if (blocking) {
        status = read_blocking(io, data, len, &actual);
    } else {
        status = zxio_read(io, data, len, &actual);
    }
    return status != ZX_OK ? status : (ssize_t)actual;
}

static ssize_t write_internal(zxio_t* io, bool blocking, const void* data, size_t len) {
    size_t actual = 0u;
    zx_status_t status = ZX_OK;
    if (blocking) {
        status = write_blocking(io, data, len, &actual);
    } else {
        status = zxio_write(io, data, len, &actual);
    }
    return status != ZX_OK ? status : (ssize_t)actual;
}

static inline zxio_pipe_t* fdio_get_zxio_pipe(fdio_t* io) {
    fdio_zxio_pipe_t* wrapper = (fdio_zxio_pipe_t*)io;
    return &wrapper->pipe;
}

static zx_status_t fdio_zxio_pipe_clone(fdio_t* io, zx_handle_t* handles, uint32_t* types) {
    zxio_pipe_t* pipe = fdio_get_zxio_pipe(io);
    zx_status_t status = zx_handle_duplicate(pipe->socket, ZX_RIGHT_SAME_RIGHTS,
                                             &handles[0]);
    if (status != ZX_OK) {
        return status;
    }
    types[0] = PA_FDIO_SOCKET;
    return 1;
}

static zx_status_t fdio_zxio_pipe_unwrap(fdio_t* io, zx_handle_t* handles,
                                         uint32_t* types) {
    zxio_t* z = fdio_get_zxio(io);
    zx_handle_t handle = ZX_HANDLE_INVALID;
    zx_status_t status = zxio_release(z, &handle);
    if (status != ZX_OK) {
        return status;
    }
    handles[0] = handle;
    types[0] = PA_FDIO_SOCKET;
    return 1;
}

static ssize_t fdio_zxio_pipe_read(fdio_t* io, void* data, size_t len) {
    zxio_t* z = fdio_get_zxio(io);
    bool blocking = !(io->ioflag & IOFLAG_NONBLOCK);
    return read_internal(z, blocking, data, len);
}

static ssize_t fdio_zxio_pipe_write(fdio_t* io, const void* data, size_t len) {
    zxio_t* z = fdio_get_zxio(io);
    bool blocking = !(io->ioflag & IOFLAG_NONBLOCK);
    return write_internal(z, blocking, data, len);
}

static ssize_t fdio_zxio_pipe_posix_ioctl(fdio_t* io, int request, va_list va) {
    zxio_pipe_t* pipe = fdio_get_zxio_pipe(io);
    switch (request) {
    case FIONREAD: {
        size_t available;
        zx_status_t status = zx_socket_read(pipe->socket, 0, NULL, 0, &available);
        if (status != ZX_OK) {
            return status;
        }
        if (available > INT_MAX) {
            available = INT_MAX;
        }
        int* actual = va_arg(va, int*);
        *actual = available;
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static ssize_t fdio_zxio_pipe_recvfrom(fdio_t* io, void* data, size_t len, int flags,
                                       struct sockaddr* restrict addr,
                                       socklen_t* restrict addrlen) {
    if (flags & ~MSG_DONTWAIT) {
        return ZX_ERR_INVALID_ARGS;
    }
    zxio_t* z = fdio_get_zxio(io);
    bool blocking = !((io->ioflag & IOFLAG_NONBLOCK) || (flags & MSG_DONTWAIT));
    return read_internal(z, blocking, data, len);
}

static ssize_t fdio_zxio_pipe_sendto(fdio_t* io, const void* data, size_t len, int flags, const struct sockaddr* addr, socklen_t addrlen) {
    if (flags & ~MSG_DONTWAIT) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (addr != NULL) {
        return ZX_ERR_INVALID_ARGS; // should set errno to EISCONN
    }
    zxio_t* z = fdio_get_zxio(io);
    bool blocking = !((io->ioflag & IOFLAG_NONBLOCK) || (flags & MSG_DONTWAIT));
    return write_internal(z, blocking, data, len);
}

static ssize_t fdio_zxio_pipe_recvmsg(fdio_t* io, struct msghdr* msg, int flags) {
    // we ignore msg_name and msg_namelen members.
    // (this is a consistent behavior with other OS implementations for TCP protocol)
    zxio_t* z = fdio_get_zxio(io);
    ssize_t total = 0;
    ssize_t n = 0;
    bool blocking = !((io->ioflag & IOFLAG_NONBLOCK) || (flags & MSG_DONTWAIT));
    for (int i = 0; i < msg->msg_iovlen; i++) {
        struct iovec* iov = &msg->msg_iov[i];
        n = read_internal(z, blocking, iov->iov_base, iov->iov_len);
        if (n > 0) {
            total += n;
        }
        if ((size_t)n != iov->iov_len) {
            break;
        }
    }
    return total > 0 ? total : n;
}

static ssize_t fdio_zxio_pipe_sendmsg(fdio_t* io, const struct msghdr* msg, int flags) {
    // Note: flags typically are used to express intent _not_ to issue SIGPIPE
    // via MSG_NOSIGNAL. Applications use this frequently to avoid having to
    // install additional signal handlers to handle cases where connection has
    // been closed by remote end.

    zxio_t* z = fdio_get_zxio(io);
    ssize_t total = 0;
    ssize_t n = 0;
    bool blocking = !((io->ioflag & IOFLAG_NONBLOCK) || (flags & MSG_DONTWAIT));
    for (int i = 0; i < msg->msg_iovlen; i++) {
        struct iovec* iov = &msg->msg_iov[i];
        if (iov->iov_len <= 0) {
            return ZX_ERR_INVALID_ARGS;
        }
        n = write_internal(z, blocking, iov->iov_base, iov->iov_len);
        if (n > 0) {
            total += n;
        }
        if ((size_t)n != iov->iov_len) {
            break;
        }
    }
    return total > 0 ? total : n;
}

static zx_status_t fdio_zxio_pipe_shutdown(fdio_t* io, int how) {
    uint32_t options = 0;
    switch (how) {
    case SHUT_RD:
        options = ZX_SOCKET_SHUTDOWN_READ;
        break;
    case SHUT_WR:
        options = ZX_SOCKET_SHUTDOWN_WRITE;
        break;
    case SHUT_RDWR:
        options = ZX_SOCKET_SHUTDOWN_READ | ZX_SOCKET_SHUTDOWN_WRITE;
        break;
    }
    zxio_pipe_t* pipe = fdio_get_zxio_pipe(io);
    return zx_socket_write(pipe->socket, options, NULL, 0, NULL);
}

static fdio_ops_t fdio_zxio_pipe_ops = {
    .read = fdio_zxio_pipe_read,
    .read_at = fdio_default_read_at,
    .write = fdio_zxio_pipe_write,
    .write_at = fdio_default_write_at,
    .seek = fdio_default_seek,
    .misc = fdio_default_misc,
    .close = fdio_zxio_close,
    .open = fdio_default_open,
    .clone = fdio_zxio_pipe_clone,
    .ioctl = fdio_default_ioctl,
    .wait_begin = fdio_zxio_wait_begin,
    .wait_end = fdio_zxio_wait_end,
    .unwrap = fdio_zxio_pipe_unwrap,
    .posix_ioctl = fdio_zxio_pipe_posix_ioctl,
    .get_vmo = fdio_default_get_vmo,
    .get_token = fdio_default_get_token,
    .get_attr = fdio_zxio_get_attr,
    .set_attr = fdio_zxio_set_attr,
    .sync = fdio_default_sync,
    .readdir = fdio_default_readdir,
    .rewind = fdio_default_rewind,
    .unlink = fdio_default_unlink,
    .truncate = fdio_zxio_truncate,
    .rename = fdio_default_rename,
    .link = fdio_default_link,
    .get_flags = fdio_default_get_flags,
    .set_flags = fdio_default_set_flags,
    .recvfrom = fdio_zxio_pipe_recvfrom,
    .sendto = fdio_zxio_pipe_sendto,
    .recvmsg = fdio_zxio_pipe_recvmsg,
    .sendmsg = fdio_zxio_pipe_sendmsg,
    .shutdown = fdio_zxio_pipe_shutdown,
};

fdio_t* fdio_zxio_create_pipe(zx_handle_t socket) {
    fdio_zxio_pipe_t* fv = fdio_alloc(sizeof(fdio_zxio_pipe_t));
    if (fv == NULL) {
        zx_handle_close(socket);
        return NULL;
    }
    fv->io.ops = &fdio_zxio_pipe_ops;
    fv->io.magic = FDIO_MAGIC;
    atomic_init(&fv->io.refcount, 1);
    zx_status_t status = zxio_pipe_init(&fv->pipe, socket);
    if (status != ZX_OK) {
        return NULL;
    }
    return &fv->io;
}
