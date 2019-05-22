// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/fdio/unsafe.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <lib/zxio/zxio.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <zircon/assert.h>
#include <zircon/device/ioctl.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include "private.h"

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

static zx_status_t fdio_zxio_clone(fdio_t* io, zx_handle_t* out_handle) {
    zxio_t* z = fdio_get_zxio(io);
    return zxio_clone(z, out_handle);
}

static zx_status_t fdio_zxio_unwrap(fdio_t* io, zx_handle_t* out_handle) {
    zxio_t* z = fdio_get_zxio(io);
    return zxio_release(z, out_handle);
}

static zx_status_t fdio_zxio_sync(fdio_t* io) {
    zxio_t* z = fdio_get_zxio(io);
    return zxio_sync(z);
}

static zx_status_t fdio_zxio_get_attr(fdio_t* io, fuchsia_io_NodeAttributes* out) {
    zxio_t* z = fdio_get_zxio(io);
    return zxio_attr_get(z, out);
}

static zx_status_t fdio_zxio_set_attr(fdio_t* io, uint32_t flags, const fuchsia_io_NodeAttributes* attr) {
    zxio_t* z = fdio_get_zxio(io);
    return zxio_attr_set(z, flags, attr);
}

static ssize_t fdio_zxio_read(fdio_t* io, void* data, size_t len) {
    zxio_t* z = fdio_get_zxio(io);
    size_t actual = 0;
    zx_status_t status = zxio_read(z, data, len, &actual);
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

// Generic ---------------------------------------------------------------------

fdio_ops_t fdio_zxio_ops = {
    .close = fdio_zxio_close,
    .open = fdio_default_open,
    .clone = fdio_zxio_clone,
    .unwrap = fdio_zxio_unwrap,
    .wait_begin = fdio_zxio_wait_begin,
    .wait_end = fdio_zxio_wait_end,
    .ioctl = fdio_default_ioctl,
    .posix_ioctl = fdio_default_posix_ioctl,
    .get_vmo = fdio_default_get_vmo,
    .get_token = fdio_default_get_token,
    .get_attr = fdio_zxio_get_attr,
    .set_attr = fdio_zxio_set_attr,
    .readdir = fdio_default_readdir,
    .rewind = fdio_default_rewind,
    .unlink = fdio_default_unlink,
    .truncate = fdio_zxio_truncate,
    .rename = fdio_default_rename,
    .link = fdio_default_link,
    .get_flags = fdio_zxio_get_flags,
    .set_flags = fdio_zxio_set_flags,
    .recvfrom = fdio_default_recvfrom,
    .sendto = fdio_default_sendto,
    .recvmsg = fdio_default_recvmsg,
    .sendmsg = fdio_default_sendmsg,
    .shutdown = fdio_default_shutdown,
    .get_rcvtimeo = fdio_default_get_rcvtimeo,
};

__EXPORT
fdio_t* fdio_zxio_create(zxio_storage_t** out_storage) {
    fdio_t* io = fdio_alloc(&fdio_zxio_ops);
    if (io == NULL) {
        return NULL;
    }
    zxio_null_init(&fdio_get_zxio_storage(io)->io);
    *out_storage = fdio_get_zxio_storage(io);
    return io;
}

// Null ------------------------------------------------------------------------

__EXPORT
fdio_t* fdio_null_create(void) {
    zxio_storage_t* storage = NULL;
    return fdio_zxio_create(&storage);
}

// Remote ----------------------------------------------------------------------

// POLL_MASK and POLL_SHIFT intend to convert the lower five POLL events into
// ZX_USER_SIGNALs and vice-versa. Other events need to be manually converted to
// a zx_signals_t, if they are desired.
#define POLL_SHIFT  24
#define POLL_MASK   0x1F

static zxio_remote_t* fdio_get_zxio_remote(fdio_t* io) {
    return (zxio_remote_t*)fdio_get_zxio(io);
}

static zx_status_t fdio_zxio_remote_open(fdio_t* io, const char* path,
                                         uint32_t flags, uint32_t mode,
                                         fdio_t** out) {
    zxio_remote_t* rio = fdio_get_zxio_remote(io);
    return fdio_remote_open_at(rio->control, path, flags, mode, out);
}

static zx_status_t fidl_ioctl(zx_handle_t h, uint32_t op, const void* in_buf,
                              size_t in_len, void* out_buf, size_t out_len,
                              size_t* out_actual) {
    size_t in_handle_count = 0;
    size_t out_handle_count = 0;
    switch (IOCTL_KIND(op)) {
    case IOCTL_KIND_GET_HANDLE:
        out_handle_count = 1;
        break;
    case IOCTL_KIND_GET_TWO_HANDLES:
        out_handle_count = 2;
        break;
    case IOCTL_KIND_GET_THREE_HANDLES:
        out_handle_count = 3;
        break;
    case IOCTL_KIND_SET_HANDLE:
        in_handle_count = 1;
        break;
    case IOCTL_KIND_SET_TWO_HANDLES:
        in_handle_count = 2;
        break;
    }

    if (in_len < in_handle_count * sizeof(zx_handle_t)) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (out_len < out_handle_count * sizeof(zx_handle_t)) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_handle_t hbuf[out_handle_count];
    size_t out_handle_actual;
    zx_status_t io_status, status;
    if ((io_status = fuchsia_io_NodeIoctl(h, op,
                                          out_len, static_cast<const zx_handle_t*>(in_buf),
                                          in_handle_count, static_cast<const uint8_t*>(in_buf),
                                          in_len, &status, hbuf,
                                          out_handle_count, &out_handle_actual,
                                          static_cast<uint8_t*>(out_buf), out_len, out_actual)) != ZX_OK) {
        return io_status;
    }

    if (status != ZX_OK) {
        zx_handle_close_many(hbuf, out_handle_actual);
        return status;
    }
    if (out_handle_actual != out_handle_count) {
        zx_handle_close_many(hbuf, out_handle_actual);
        return ZX_ERR_IO;
    }

    memcpy(out_buf, hbuf, out_handle_count * sizeof(zx_handle_t));
    return ZX_OK;
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

static zx_status_t fdio_zxio_remote_get_vmo(fdio_t* io, int flags, zx_handle_t* out_vmo) {
    zxio_remote_t* rio = fdio_get_zxio_remote(io);
    fuchsia_mem_Buffer buffer;
    memset(&buffer, 0, sizeof(buffer));
    zx_status_t io_status, status;
    io_status = fuchsia_io_FileGetBuffer(rio->control, flags, &status, &buffer);
    if (io_status != ZX_OK) {
        return io_status;
    }
    if (status != ZX_OK) {
        return status;
    }
    if (buffer.vmo == ZX_HANDLE_INVALID) {
        return ZX_ERR_IO;
    }
    *out_vmo = buffer.vmo;
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
    io_status = fuchsia_io_DirectoryReadDirents(rio->control, max, &status,
                                                static_cast<uint8_t*>(ptr), max, &actual);
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

static fdio_ops_t fdio_zxio_remote_ops = {
    .close = fdio_zxio_close,
    .open = fdio_zxio_remote_open,
    .clone = fdio_zxio_clone,
    .unwrap = fdio_zxio_unwrap,
    .wait_begin = fdio_zxio_remote_wait_begin,
    .wait_end = fdio_zxio_remote_wait_end,
    .ioctl = fdio_zxio_remote_ioctl,
    .posix_ioctl = fdio_default_posix_ioctl,
    .get_vmo = fdio_zxio_remote_get_vmo,
    .get_token = fdio_zxio_remote_get_token,
    .get_attr = fdio_zxio_get_attr,
    .set_attr = fdio_zxio_set_attr,
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
    .get_rcvtimeo = fdio_default_get_rcvtimeo,
};

fdio_t* fdio_remote_create(zx_handle_t control, zx_handle_t event) {
    fdio_t* io = fdio_alloc(&fdio_zxio_remote_ops);
    if (io == NULL) {
        zx_handle_close(control);
        zx_handle_close(event);
        return NULL;
    }
    zx_status_t status = zxio_remote_init(fdio_get_zxio_storage(io), control, event);
    if (status != ZX_OK) {
        return NULL;
    }
    return io;
}

fdio_t* fdio_dir_create(zx_handle_t control) {
    fdio_t* io = fdio_alloc(&fdio_zxio_remote_ops);
    if (io == NULL) {
        zx_handle_close(control);
        return NULL;
    }
    zx_status_t status = zxio_dir_init(fdio_get_zxio_storage(io), control);
    if (status != ZX_OK) {
        return NULL;
    }
    return io;
}

fdio_t* fdio_file_create(zx_handle_t control, zx_handle_t event) {
    fdio_t* io = fdio_alloc(&fdio_zxio_remote_ops);
    if (io == NULL) {
        zx_handle_close(control);
        return NULL;
    }
    zx_status_t status = zxio_file_init(fdio_get_zxio_storage(io), control, event);
    if (status != ZX_OK) {
        return NULL;
    }
    return io;
}

__EXPORT
zx_status_t fdio_get_service_handle(int fd, zx_handle_t* out) {
    mtx_lock(&fdio_lock);
    if ((fd < 0) || (fd >= FDIO_MAX_FD) || (fdio_fdtab[fd] == NULL)) {
        mtx_unlock(&fdio_lock);
        return ZX_ERR_NOT_FOUND;
    }
    fdio_t* io = fdio_fdtab[fd];
    fdio_dupcount_release(io);
    fdio_fdtab[fd] = NULL;
    if (fdio_get_dupcount(io) > 0) {
        // still alive in other fdtab slots
        // this fd goes away but we can't give away the handle
        mtx_unlock(&fdio_lock);
        fdio_release(io);
        return ZX_ERR_UNAVAILABLE;
    } else {
        mtx_unlock(&fdio_lock);
        zx_status_t r;
        if (fdio_get_ops(io) == &fdio_zxio_remote_ops) {
            zxio_remote_t* file = fdio_get_zxio_remote(io);
            r = zxio_release(&file->io, out);
        } else {
            r = ZX_ERR_NOT_SUPPORTED;
            fdio_get_ops(io)->close(io);
        }
        fdio_release(io);
        return r;
    }
}

__EXPORT
zx_handle_t fdio_unsafe_borrow_channel(fdio_t* io) {
    if (io == NULL) {
        return ZX_HANDLE_INVALID;
    }

    if (fdio_get_ops(io) == &fdio_zxio_remote_ops) {
        zxio_remote_t* file = fdio_get_zxio_remote(io);
        return file->control;
    }
    return ZX_HANDLE_INVALID;
}

// Vmo -------------------------------------------------------------------------

fdio_t* fdio_vmo_create(zx_handle_t vmo, zx_off_t seek) {
    zxio_storage_t* storage = NULL;
    fdio_t* io = fdio_zxio_create(&storage);
    if (io == NULL) {
        zx_handle_close(vmo);
        return NULL;
    }
    zx_status_t status = zxio_vmo_init(storage, vmo, seek);
    if (status != ZX_OK) {
        fdio_release(io);
        return NULL;
    }
    return io;
}

// Vmofile ---------------------------------------------------------------------

static inline zxio_vmofile_t* fdio_get_zxio_vmofile(fdio_t* io) {
    return (zxio_vmofile_t*)fdio_get_zxio(io);
}

static zx_status_t fdio_zxio_vmofile_get_vmo(fdio_t* io, int flags,
                                             zx_handle_t* out_vmo) {
    zxio_vmofile_t* file = fdio_get_zxio_vmofile(io);

    if (out_vmo == NULL) {
        return ZX_ERR_INVALID_ARGS;
    }

    size_t length = file->end - file->off;
    if (flags & fuchsia_io_VMO_FLAG_PRIVATE) {
        // Why don't we consider file->off in this branch? It seems like we
        // want to clone the part of the VMO from file->off to file->end rather
        // than length bytes at the start of the VMO.
        return zx_vmo_create_child(file->vmo, ZX_VMO_CHILD_COPY_ON_WRITE, 0, length, out_vmo);
    } else {
        size_t vmo_length = 0;
        if (file->off != 0 || zx_vmo_get_size(file->vmo, &vmo_length) != ZX_OK ||
            length != vmo_length) {
            return ZX_ERR_NOT_FOUND;
        }
        zx_rights_t rights = ZX_RIGHTS_BASIC | ZX_RIGHT_GET_PROPERTY |
                ZX_RIGHT_MAP;
        rights |= (flags & fuchsia_io_VMO_FLAG_READ) ? ZX_RIGHT_READ : 0;
        rights |= (flags & fuchsia_io_VMO_FLAG_WRITE) ? ZX_RIGHT_WRITE : 0;
        rights |= (flags & fuchsia_io_VMO_FLAG_EXEC) ? ZX_RIGHT_EXECUTE : 0;
        return zx_handle_duplicate(file->vmo, rights, out_vmo);
    }
}

fdio_ops_t fdio_zxio_vmofile_ops = {
    .close = fdio_zxio_close,
    .open = fdio_default_open,
    .clone = fdio_zxio_clone,
    .unwrap = fdio_zxio_unwrap,
    .wait_begin = fdio_default_wait_begin,
    .wait_end = fdio_default_wait_end,
    .ioctl = fdio_default_ioctl,
    .posix_ioctl = fdio_default_posix_ioctl,
    .get_vmo = fdio_zxio_vmofile_get_vmo,
    .get_token = fdio_default_get_token,
    .get_attr = fdio_zxio_get_attr,
    .set_attr = fdio_zxio_set_attr,
    .readdir = fdio_default_readdir,
    .rewind = fdio_default_rewind,
    .unlink = fdio_default_unlink,
    .truncate = fdio_zxio_truncate,
    .rename = fdio_default_rename,
    .link = fdio_default_link,
    .get_flags = fdio_zxio_get_flags,
    .set_flags = fdio_zxio_set_flags,
    .recvfrom = fdio_default_recvfrom,
    .sendto = fdio_default_sendto,
    .recvmsg = fdio_default_recvmsg,
    .sendmsg = fdio_default_sendmsg,
    .shutdown = fdio_default_shutdown,
    .get_rcvtimeo = fdio_default_get_rcvtimeo,
};

fdio_t* fdio_vmofile_create(zx_handle_t control, zx_handle_t vmo,
                            zx_off_t offset, zx_off_t length,
                            zx_off_t seek) {
    fdio_t* io = fdio_alloc(&fdio_zxio_vmofile_ops);
    if (io == NULL) {
        zx_handle_close(control);
        zx_handle_close(vmo);
        return NULL;
    }
    zx_status_t status = zxio_vmofile_init(fdio_get_zxio_storage(io), control, vmo, offset,
                                           length, seek);
    if (status != ZX_OK) {
        return NULL;
    }
    return io;
}

// Pipe ------------------------------------------------------------------------

static inline zxio_pipe_t* fdio_get_zxio_pipe(fdio_t* io) {
    return (zxio_pipe_t*)fdio_get_zxio(io);
}

static ssize_t fdio_zxio_pipe_posix_ioctl(fdio_t* io, int request, va_list va) {
    zxio_pipe_t* pipe = fdio_get_zxio_pipe(io);
    switch (request) {
    case FIONREAD: {
        zx_info_socket_t info;
        memset(&info, 0, sizeof(info));
        zx_status_t status = zx_object_get_info(pipe->socket, ZX_INFO_SOCKET,
                                                &info, sizeof(info), NULL, NULL);
        if (status != ZX_OK) {
            return status;
        }
        size_t available = info.rx_buf_available;
        if (available > INT_MAX) {
            available = INT_MAX;
        }
        int* actual = va_arg(va, int*);
        *actual = static_cast<int>(available);
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static ssize_t fdio_zxio_pipe_recvfrom(fdio_t* io, void* data, size_t len, int flags,
                                       struct sockaddr* __restrict addr,
                                       socklen_t* __restrict addrlen) {
    if (flags & ~MSG_DONTWAIT) {
        return ZX_ERR_INVALID_ARGS;
    }
    return fdio_zxio_read(io, data, len);
}

static ssize_t fdio_zxio_pipe_sendto(fdio_t* io, const void* data, size_t len, int flags, const struct sockaddr* addr, socklen_t addrlen) {
    if (flags & ~MSG_DONTWAIT) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (addr != NULL) {
        return ZX_ERR_INVALID_ARGS; // should set errno to EISCONN
    }
    return fdio_zxio_write(io, data, len);
}

static ssize_t fdio_zxio_pipe_recvmsg(fdio_t* io, struct msghdr* msg, int flags) {
    // we ignore msg_name and msg_namelen members.
    // (this is a consistent behavior with other OS implementations for TCP protocol)
    ssize_t total = 0;
    ssize_t n = 0;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        struct iovec* iov = &msg->msg_iov[i];
        n = fdio_zxio_read(io, iov->iov_base, iov->iov_len);
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
    ssize_t total = 0;
    ssize_t n = 0;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        struct iovec* iov = &msg->msg_iov[i];
        if (iov->iov_len <= 0) {
            return ZX_ERR_INVALID_ARGS;
        }
        n = fdio_zxio_write(io, iov->iov_base, iov->iov_len);
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
    return zx_socket_shutdown(pipe->socket, options);
}

static fdio_ops_t fdio_zxio_pipe_ops = {
    .close = fdio_zxio_close,
    .open = fdio_default_open,
    .clone = fdio_zxio_clone,
    .unwrap = fdio_zxio_unwrap,
    .wait_begin = fdio_zxio_wait_begin,
    .wait_end = fdio_zxio_wait_end,
    .ioctl = fdio_default_ioctl,
    .posix_ioctl = fdio_zxio_pipe_posix_ioctl,
    .get_vmo = fdio_default_get_vmo,
    .get_token = fdio_default_get_token,
    .get_attr = fdio_zxio_get_attr,
    .set_attr = fdio_zxio_set_attr,
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
    .get_rcvtimeo = fdio_default_get_rcvtimeo,
};

fdio_t* fdio_pipe_create(zx_handle_t socket) {
    fdio_t* io = fdio_alloc(&fdio_zxio_pipe_ops);
    if (io == NULL) {
        zx_handle_close(socket);
        return NULL;
    }
    zx_status_t status = zxio_pipe_init(fdio_get_zxio_storage(io), socket);
    if (status != ZX_OK) {
        return NULL;
    }
    return io;
}

fdio_t* fdio_socketpair_create(zx_handle_t h) {
    return fdio_pipe_create(h);
}

int fdio_pipe_pair(fdio_t** _a, fdio_t** _b) {
    zx_handle_t h0, h1;
    fdio_t *a, *b;
    zx_status_t r;
    if ((r = zx_socket_create(0, &h0, &h1)) < 0) {
        return r;
    }
    if ((a = fdio_pipe_create(h0)) == NULL) {
        zx_handle_close(h1);
        return ZX_ERR_NO_MEMORY;
    }
    if ((b = fdio_pipe_create(h1)) == NULL) {
        fdio_zxio_close(a);
        return ZX_ERR_NO_MEMORY;
    }
    *_a = a;
    *_b = b;
    return 0;
}

__EXPORT
zx_status_t fdio_pipe_half(zx_handle_t* handle, uint32_t* type) {
    zx_handle_t h0, h1;
    zx_status_t r;
    fdio_t* io;
    int fd;
    if ((r = zx_socket_create(0, &h0, &h1)) < 0) {
        return r;
    }
    if ((io = fdio_pipe_create(h0)) == NULL) {
        r = ZX_ERR_NO_MEMORY;
        goto fail;
    }
    if ((fd = fdio_bind_to_fd(io, -1, 0)) < 0) {
        fdio_release(io);
        r = ZX_ERR_NO_RESOURCES;
        goto fail;
    }
    *handle = h1;
    *type = PA_FD;
    return fd;

fail:
    zx_handle_close(h1);
    return r;
}

__EXPORT
zx_status_t fdio_pipe_half2(int* out_fd, zx_handle_t* out_handle) {
    zx_handle_t h0, h1;
    zx_status_t r;
    fdio_t* io;
    if ((r = zx_socket_create(0, &h0, &h1)) < 0) {
        return r;
    }
    if ((io = fdio_pipe_create(h0)) == NULL) {
        r = ZX_ERR_NO_MEMORY;
        goto fail;
    }
    if ((*out_fd = fdio_bind_to_fd(io, -1, 0)) < 0) {
        fdio_release(io);
        r = ZX_ERR_NO_RESOURCES;
        goto fail;
    }
    *out_handle = h1;
    return ZX_OK;

fail:
    zx_handle_close(h1);
    return r;
}

// Debuglog --------------------------------------------------------------------

fdio_t* fdio_logger_create(zx_handle_t handle) {
    zxio_storage_t* storage = NULL;
    fdio_t* io = fdio_zxio_create(&storage);
    if (io == NULL) {
        zx_handle_close(handle);
        return NULL;
    }
    zx_status_t status = zxio_debuglog_init(storage, handle);
    ZX_ASSERT(status == ZX_OK);
    return io;
}
