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

#include <lib/fdio/debug.h>
#include <lib/fdio/io.fidl.h>
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

static const char* _opnames[] = ZXRIO_OPNAMES;
const char* fdio_opname(uint32_t op) {
    op = ZXRIO_OPNAME(op);
    if (op < ZXRIO_NUM_OPS) {
        return _opnames[op];
    } else {
        return "unknown";
    }
}

static void discard_handles(zx_handle_t* handles, unsigned count) {
    while (count-- > 0) {
        zx_handle_close(*handles++);
    }
}

zx_status_t zxrio_handle_rpc(zx_handle_t h, zxrio_msg_t* msg, zxrio_cb_t cb, void* cookie) {
    zx_status_t r = zxrio_read_request(h, msg);
    if (r != ZX_OK) {
        return r;
    }
    bool is_close = (ZXRIO_OP(msg->op) == ZXRIO_CLOSE) ||
                    (ZXRIO_OP(msg->op) == ZXFIDL_CLOSE);

    r = cb(msg, cookie);
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

    r = zxrio_write_response(h, r, msg);

    if (is_close) {
        // signals to not perform a close callback
        return ERR_DISPATCHER_DONE;
    } else {
        return r;
    }
}

zx_status_t zxrio_handle_close(zxrio_cb_t cb, void* cookie) {
    zxrio_msg_t msg;

    // remote side was closed;
#ifdef ZXRIO_FIDL
    fuchsia_io_ObjectCloseRequest* request = (fuchsia_io_ObjectCloseRequest*) &msg;
    memset(request, 0, sizeof(fuchsia_io_ObjectCloseRequest));
    request->hdr.ordinal = ZXFIDL_CLOSE;
#else
    msg.op = ZXRIO_CLOSE;
    msg.arg = 0;
    msg.datalen = 0;
    msg.hcount = 0;
#endif
    cb(&msg, cookie);
    return ERR_DISPATCHER_DONE;
}

zx_status_t zxrio_handler(zx_handle_t h, zxrio_cb_t cb, void* cookie) {
    if (h == ZX_HANDLE_INVALID) {
        return zxrio_handle_close(cb, cookie);
    } else {
        char buffer[ZX_CHANNEL_MAX_MSG_BYTES];
        return zxrio_handle_rpc(h, (zxrio_msg_t*) buffer, cb, cookie);
    }
}

zx_status_t zxrio_txn_handoff(zx_handle_t srv, zx_handle_t reply, zxrio_msg_t* msg) {
    msg->txid = 0;
    uint32_t dsize;
    switch (msg->op) {
    case ZXFIDL_OPEN: {
        fuchsia_io_DirectoryOpenRequest* request = (fuchsia_io_DirectoryOpenRequest*) msg;
        request->object = FIDL_HANDLE_PRESENT;
        dsize = FIDL_ALIGN(sizeof(fuchsia_io_DirectoryOpenRequest)) +
            FIDL_ALIGN(request->path.size);
        break;
    }
    case ZXFIDL_CLONE: {
        fuchsia_io_ObjectCloneRequest* request = (fuchsia_io_ObjectCloneRequest*) msg;
        request->object = FIDL_HANDLE_PRESENT;
        dsize = sizeof(fuchsia_io_ObjectCloneRequest);
        break;
    }
    default:
        ZX_DEBUG_ASSERT(!ZXRIO_FIDL_MSG(msg->op));
        msg->handle[0] = reply;
        msg->hcount = 1;
        dsize = ZXRIO_HDR_SZ + msg->datalen;
    }

    zx_status_t r;
    if ((r = zx_channel_write(srv, 0, msg, dsize, &reply, 1)) != ZX_OK) {
        printf("zxrio_txn_handoff: Failed to write\n");
    }
    return r;
}

// on success, msg->hcount indicates number of valid handles in msg->handle
// on error there are never any handles
static zx_status_t zxrio_txn(zxrio_t* rio, zxrio_msg_t* msg) {
    if (!is_rio_message_valid(msg)) {
        return ZX_ERR_INVALID_ARGS;
    }

    xprintf("txn h=%x op=%d len=%u\n", rio->h, msg->op, msg->datalen);

    zx_status_t r;
    uint32_t dsize;

    zx_channel_call_args_t args;
    args.wr_bytes = msg;
    args.wr_handles = msg->handle;
    args.rd_bytes = msg;
    args.rd_handles = msg->handle;
    args.wr_num_bytes = ZXRIO_HDR_SZ + msg->datalen;
    args.wr_num_handles = msg->hcount;
    args.rd_num_bytes = ZXRIO_HDR_SZ + FDIO_CHUNK_SIZE;
    args.rd_num_handles = FDIO_MAX_HANDLES;
    const uint32_t request_op = ZXRIO_OP(msg->op);

    r = zx_channel_call(rio->h, 0, ZX_TIME_INFINITE, &args, &dsize, &msg->hcount);
    if (r < 0) {
        msg->hcount = 0;
        return r;
    }

    // check for protocol errors
    if (!is_rio_message_reply_valid(msg, dsize) ||
        (ZXRIO_OP(msg->op) != request_op)) {
        r = ZX_ERR_IO;
        goto fail_discard_handles;
    }
    // check for remote error
    if ((r = msg->arg) < 0) {
        goto fail_discard_handles;
    }
    return r;

fail_discard_handles:
    // If we failed after reading, we need to abandon any handles we received.
    discard_handles(msg->handle, msg->hcount);
    msg->hcount = 0;
    return r;
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

#ifdef ZXRIO_FIDL

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
    case ZXRIO_CLONE:
        r = fidl_clone_request(svc, cnxn, flags);
        break;
    case ZXRIO_OPEN:
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
    case ZXRIO_CLONE:
        r = fidl_clone_request(svc, cnxn, flags);
        break;
    case ZXRIO_OPEN:
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

#else // ZXRIO_FIDL

zx_status_t zxrio_close(fdio_t* io) {
    zxrio_t* rio = (zxrio_t*)io;
    zxrio_msg_t msg;
    zx_status_t r;

    memset(&msg, 0, ZXRIO_HDR_SZ);
    msg.op = ZXRIO_CLOSE;

    if ((r = zxrio_txn(rio, &msg)) >= 0) {
        discard_handles(msg.handle, msg.hcount);
    }

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
    zxrio_msg_t msg;
    memset(&msg, 0, ZXRIO_HDR_SZ);
    msg.op = op;
    msg.datalen = pathlen;
    msg.arg = flags;
    msg.arg2.mode = mode;
    memcpy(msg.data, path, pathlen);

    zx_status_t r;
    zx_handle_t h;
    if ((r = zx_channel_create(0, &h, &msg.handle[0])) < 0) {
        return r;
    }
    msg.hcount = 1;

    // Write the (one-way) request message
    if ((r = zx_channel_write(svc, 0, &msg, ZXRIO_HDR_SZ + msg.datalen,
                              msg.handle, msg.hcount)) < 0) {
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

    zxrio_msg_t msg;
    memset(&msg, 0, ZXRIO_HDR_SZ);
    msg.op = op;
    msg.datalen = len;
    msg.arg = flags;
    msg.arg2.mode = mode;
    msg.hcount = 1;
    msg.handle[0] = cnxn;
    memcpy(msg.data, name, len);

    return zx_channel_write(svc, 0, &msg, ZXRIO_HDR_SZ + msg.datalen, msg.handle, 1);
}

static ssize_t write_common(uint32_t op, fdio_t* io, const void* _data, size_t len, off_t offset) {
    zxrio_t* rio = (zxrio_t*)io;
    const uint8_t* data = _data;
    ssize_t count = 0;
    zx_status_t r = 0;
    zxrio_msg_t msg;
    ssize_t xfer;

    while (len > 0) {
        xfer = (len > FDIO_CHUNK_SIZE) ? FDIO_CHUNK_SIZE : len;

        memset(&msg, 0, ZXRIO_HDR_SZ);
        msg.op = op;
        msg.datalen = xfer;
        if (op == ZXRIO_WRITE_AT)
            msg.arg2.off = offset;
        memcpy(msg.data, data, xfer);

        if ((r = zxrio_txn(rio, &msg)) < 0) {
            break;
        }
        discard_handles(msg.handle, msg.hcount);

        if (r > xfer) {
            r = ZX_ERR_IO;
            break;
        }
        count += r;
        data += r;
        len -= r;
        if (op == ZXRIO_WRITE_AT)
            offset += r;
        // stop at short read
        if (r < xfer) {
            break;
        }
    }
    return count ? count : r;
}

static ssize_t zxrio_write(fdio_t* io, const void* _data, size_t len) {
    return write_common(ZXRIO_WRITE, io, _data, len, 0);
}

static ssize_t zxrio_write_at(fdio_t* io, const void* _data, size_t len, off_t offset) {
    return write_common(ZXRIO_WRITE_AT, io, _data, len, offset);
}

static ssize_t read_common(uint32_t op, fdio_t* io, void* _data, size_t len, off_t offset) {
    zxrio_t* rio = (zxrio_t*)io;
    uint8_t* data = _data;
    ssize_t count = 0;
    zx_status_t r = 0;
    zxrio_msg_t msg;
    ssize_t xfer;

    while (len > 0) {
        xfer = (len > FDIO_CHUNK_SIZE) ? FDIO_CHUNK_SIZE : len;

        memset(&msg, 0, ZXRIO_HDR_SZ);
        msg.op = op;
        msg.arg = xfer;
        if (op == ZXRIO_READ_AT)
            msg.arg2.off = offset;

        if ((r = zxrio_txn(rio, &msg)) < 0) {
            break;
        }
        discard_handles(msg.handle, msg.hcount);

        if ((r > (int)msg.datalen) || (r > xfer)) {
            r = ZX_ERR_IO;
            break;
        }
        memcpy(data, msg.data, r);
        count += r;
        data += r;
        len -= r;
        if (op == ZXRIO_READ_AT)
            offset += r;

        // stop at short read
        if (r < xfer) {
            break;
        }
    }
    return count ? count : r;
}

static ssize_t zxrio_read(fdio_t* io, void* _data, size_t len) {
    return read_common(ZXRIO_READ, io, _data, len, 0);
}

static ssize_t zxrio_read_at(fdio_t* io, void* _data, size_t len, off_t offset) {
    return read_common(ZXRIO_READ_AT, io, _data, len, offset);
}

static off_t zxrio_seek(fdio_t* io, off_t offset, int whence) {
    zxrio_t* rio = (zxrio_t*)io;
    zxrio_msg_t msg;
    zx_status_t r;

    memset(&msg, 0, ZXRIO_HDR_SZ);
    msg.op = ZXRIO_SEEK;
    msg.arg2.off = offset;
    msg.arg = whence;

    if ((r = zxrio_txn(rio, &msg)) < 0) {
        return r;
    }

    discard_handles(msg.handle, msg.hcount);
    return msg.arg2.off;
}

ssize_t zxrio_ioctl(fdio_t* io, uint32_t op, const void* in_buf,
                    size_t in_len, void* out_buf, size_t out_len) {
    zxrio_t* rio = (zxrio_t*)io;
    const uint8_t* data = in_buf;
    zx_status_t r = 0;
    zxrio_msg_t msg;

    if (in_len > FDIO_IOCTL_MAX_INPUT || out_len > FDIO_CHUNK_SIZE) {
        return ZX_ERR_INVALID_ARGS;
    }

    memset(&msg, 0, ZXRIO_HDR_SZ);
    msg.op = ZXRIO_IOCTL;
    msg.datalen = in_len;
    msg.arg = out_len;
    msg.arg2.op = op;

    switch (IOCTL_KIND(op)) {
    case IOCTL_KIND_GET_HANDLE:
        if (out_len < sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        break;
    case IOCTL_KIND_GET_TWO_HANDLES:
        if (out_len < 2 * sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        break;
    case IOCTL_KIND_GET_THREE_HANDLES:
        if (out_len < 3 * sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        break;
    case IOCTL_KIND_SET_HANDLE:
        msg.op = ZXRIO_IOCTL_1H;
        if (in_len < sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        msg.hcount = 1;
        msg.handle[0] = *((zx_handle_t*) in_buf);
        break;
    case IOCTL_KIND_SET_TWO_HANDLES:
        msg.op = ZXRIO_IOCTL_2H;
        if (in_len < 2 * sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        msg.hcount = 2;
        msg.handle[0] = *((zx_handle_t*) in_buf);
        msg.handle[1] = *(((zx_handle_t*) in_buf) + 1);
        break;
    }

    memcpy(msg.data, data, in_len);

    if ((r = zxrio_txn(rio, &msg)) < 0) {
        return r;
    }

    size_t copy_len = msg.datalen;
    if (msg.datalen > out_len) {
        copy_len = out_len;
    }

    memcpy(out_buf, msg.data, copy_len);

    int handles = 0;
    switch (IOCTL_KIND(op)) {
        case IOCTL_KIND_GET_HANDLE:
            handles = (msg.hcount > 0 ? 1 : 0);
            if (handles) {
                memcpy(out_buf, msg.handle, sizeof(zx_handle_t));
            } else {
                memset(out_buf, 0, sizeof(zx_handle_t));
            }
            break;
        case IOCTL_KIND_GET_TWO_HANDLES:
            handles = (msg.hcount > 2 ? 2 : msg.hcount);
            if (handles) {
                memcpy(out_buf, msg.handle, handles * sizeof(zx_handle_t));
            }
            if (handles < 2) {
                memset(out_buf, 0, (2 - handles) * sizeof(zx_handle_t));
            }
            break;
        case IOCTL_KIND_GET_THREE_HANDLES:
            handles = (msg.hcount > 3 ? 3 : msg.hcount);
            if (handles) {
                memcpy(out_buf, msg.handle, handles * sizeof(zx_handle_t));
            }
            if (handles < 3) {
                memset(out_buf, 0, (3 - handles) * sizeof(zx_handle_t));
            }
            break;
    }
    discard_handles(msg.handle + handles, msg.hcount - handles);

    LOG(1, "rio: close(%p)\n", io);
    return r;
}

#endif // ZXRIO_FIDL

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
    if (dsize < ZXRIO_DESCRIBE_HDR_SZ || info->op != ZXRIO_ON_OPEN) {
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

    switch (info->extra.tag) {
    // Case: No extra handles expected
    case FDIO_PROTOCOL_SERVICE:
    case FDIO_PROTOCOL_DIRECTORY:
        if (extra_handle != ZX_HANDLE_INVALID) {
            zx_handle_close(extra_handle);
            return ZX_ERR_IO;
        }
        break;
    // Case: Extra handles optional
    case FDIO_PROTOCOL_FILE:
        info->extra.file.e = extra_handle;
        break;
    case FDIO_PROTOCOL_DEVICE:
        info->extra.device.e = extra_handle;
        break;
    case FDIO_PROTOCOL_SOCKET:
        info->extra.socket.s = extra_handle;
        break;
    // Case: Extra handles required
    case FDIO_PROTOCOL_PIPE:
        if (extra_handle == ZX_HANDLE_INVALID) {
            return ZX_ERR_IO;
        }
        info->extra.pipe.s = extra_handle;
        break;
    case FDIO_PROTOCOL_VMOFILE:
        if (extra_handle == ZX_HANDLE_INVALID) {
            return ZX_ERR_IO;
        }
        info->extra.vmofile.v = extra_handle;
        break;
    default:
        printf("Unexpected protocol type opening connection\n");
        if (extra_handle != ZX_HANDLE_INVALID) {
            zx_handle_close(extra_handle);
        }
        return ZX_ERR_IO;
    }

    return r;
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
    return zxrio_connect(dir, h, ZXRIO_OPEN, ZX_FS_RIGHT_READABLE |
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
    return zxrio_connect(dir, h, ZXRIO_OPEN, flags, 0755, path);
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
    if ((r = zxrio_connect(svc, srv, ZXRIO_CLONE, ZX_FS_RIGHT_READABLE |
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
    return zxrio_connect(svc, srv, ZXRIO_CLONE, ZX_FS_RIGHT_READABLE |
                         ZX_FS_RIGHT_WRITABLE, 0755, "");
}

zx_status_t zxrio_misc(fdio_t* io, uint32_t op, int64_t off,
                       uint32_t maxreply, void* ptr, size_t len) {
    zxrio_t* rio = (zxrio_t*)io;
    zx_status_t r;

#ifdef ZXRIO_FIDL
    // Reroute FIDL operations
    switch (op) {
    case ZXRIO_STAT: {
        size_t out_sz;
        if ((r = fidl_stat(rio, maxreply, ptr, &out_sz)) != ZX_OK) {
            return r;
        }
        return out_sz;
    }
    case ZXRIO_SETATTR: {
        if (len != sizeof(vnattr_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        return fidl_setattr(rio, (const vnattr_t*) ptr);
    }
    case ZXRIO_SYNC: {
        return fidl_sync(rio);
    }
    case ZXRIO_READDIR: {
        switch (off) {
        case READDIR_CMD_RESET:
            if ((r = fidl_rewind(rio)) != ZX_OK) {
                return r;
            }
            // Fall-through to CMD_NONE
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
    case ZXRIO_UNLINK: {
        return fidl_unlink(rio, ptr, len);
    }
    case ZXRIO_TRUNCATE: {
        return fidl_truncate(rio, off);
    }
    case ZXRIO_RENAME: {
        size_t srclen = strlen(ptr);
        size_t dstlen = len - (srclen + 2);
        const char* src = ptr;
        const char* dst = ptr + srclen + 1;
        return fidl_rename(rio, src, srclen, (zx_handle_t) off, dst, dstlen);
    }
    case ZXRIO_LINK: {
        size_t srclen = strlen(ptr);
        size_t dstlen = len - (srclen + 2);
        const char* src = ptr;
        const char* dst = ptr + srclen + 1;
        return fidl_link(rio, src, srclen, (zx_handle_t) off, dst, dstlen);
    }
    case ZXRIO_FCNTL: {
        // zxrio_misc is extremely overloaded, so the interpretation
        // of these arguments can seem somewhat obtuse.
        uint32_t fcntl_op = maxreply;
        switch (fcntl_op) {
        case F_GETFL: {
            uint32_t* outflags = ptr;
            return fidl_getflags(rio, outflags);
        }
        case F_SETFL: {
            uint32_t flags = off;
            return fidl_setflags(rio, flags);
        }
        default:
            return ZX_ERR_NOT_SUPPORTED;
        }
    }
    case ZXRIO_MMAP: {
        if (len != sizeof(zxrio_mmap_data_t)) {
            printf("fdio/remoteio.c: ZXRIO_MMAP: Bad args\n");
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
    }
#endif // ZXRIO_FIDL

    zxrio_msg_t msg;

    if ((len > FDIO_CHUNK_SIZE) || (maxreply > FDIO_CHUNK_SIZE)) {
        return ZX_ERR_INVALID_ARGS;
    }

    memset(&msg, 0, ZXRIO_HDR_SZ);
    msg.op = op;
    msg.arg = maxreply;
    msg.arg2.off = off;
    msg.datalen = len;
    if (ptr && len > 0) {
        memcpy(msg.data, ptr, len);
    }
    switch (op) {
    case ZXRIO_RENAME:
    case ZXRIO_LINK:
        // As a hack, 'Rename' and 'Link' take token handles through
        // the offset argument.
        msg.handle[0] = (zx_handle_t) off;
        msg.hcount = 1;
    }

    if ((r = zxrio_txn(rio, &msg)) < 0) {
        return r;
    }

    switch (op) {
    case ZXRIO_MMAP: {
        // Ops which receive single handles:
        if ((msg.hcount != 1) || (msg.datalen > maxreply)) {
            discard_handles(msg.handle, msg.hcount);
            return ZX_ERR_IO;
        }
        r = msg.handle[0];
        memcpy(ptr, msg.data, msg.datalen);
        break;
    }
    case ZXRIO_FCNTL:
        // This is a bit of a hack, but for this case, we
        // return 'msg.arg2.mode' in the data field to simplify
        // this call for the client.
        discard_handles(msg.handle, msg.hcount);
        if (ptr) {
            memcpy(ptr, &msg.arg2.mode, sizeof(msg.arg2.mode));
        }
        break;
    default:
        // Ops which don't receive handles:
        discard_handles(msg.handle, msg.hcount);
        if (msg.datalen > maxreply) {
            return ZX_ERR_IO;
        }
        if (ptr && msg.datalen > 0) {
            memcpy(ptr, msg.data, msg.datalen);
        }
    }
    return r;
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
        if ((r = zxrio_connect(rio_h, h1, ZXRIO_OPEN, flags, mode, name)) < 0) {
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
    zx_status_t r = zxrio_getobject(h, ZXRIO_OPEN, path, flags, mode, &info, &control_channel);
    if (r < 0) {
        return r;
    }
    return fdio_from_handles(control_channel, &info.extra, out);
}

zx_status_t zxrio_open_handle_raw(zx_handle_t h, const char* path, uint32_t flags,
                                  uint32_t mode, zx_handle_t *out) {
    zx_handle_t control_channel;
    zxrio_describe_t info;
    zx_status_t r = zxrio_getobject(h, ZXRIO_OPEN, path, flags, mode, &info, &control_channel);
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
    zx_status_t r = zxrio_getobject(rio->h, ZXRIO_CLONE, "", ZX_FS_FLAG_DESCRIBE, 0, &info, &h);
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
