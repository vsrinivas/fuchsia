// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxio/inception.h>
#include <lib/zxio/zxio.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
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
