// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <threads.h>

#include <magenta/device/device.h>
#include <magenta/device/ioctl.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>

#include <mxio/debug.h>
#include <mxio/dispatcher.h>
#include <mxio/io.h>
#include <mxio/remoteio.h>
#include <mxio/socket.h>
#include <mxio/util.h>

#include "private.h"

#define MXDEBUG 0

#define POLL_SHIFT  24
#define POLL_MASK   0x1F

static_assert(MX_USER_SIGNAL_0 == (1 << POLL_SHIFT), "");
static_assert((POLLIN << POLL_SHIFT) == DEVICE_SIGNAL_READABLE, "");
static_assert((POLLPRI << POLL_SHIFT) == DEVICE_SIGNAL_OOB, "");
static_assert((POLLOUT << POLL_SHIFT) == DEVICE_SIGNAL_WRITABLE, "");
static_assert((POLLERR << POLL_SHIFT) == DEVICE_SIGNAL_ERROR, "");
static_assert((POLLHUP << POLL_SHIFT) == DEVICE_SIGNAL_HANGUP, "");

typedef struct mxrio mxrio_t;
struct mxrio {
    // base mxio io object
    mxio_t io;

    // channel handle for rpc
    mx_handle_t h;

    // event handle for device state signals, or socket handle
    mx_handle_t h2;

    // transaction id used for synchronous remoteio calls
    _Atomic mx_txid_t txid;
};

static pthread_key_t rchannel_key;

static void rchannel_cleanup(void* data) {
    if (data == NULL) {
        return;
    }
    mx_handle_t* handles = (mx_handle_t*)data;
    if (handles[0] != MX_HANDLE_INVALID)
        mx_handle_close(handles[0]);
    if (handles[1] != MX_HANDLE_INVALID)
        mx_handle_close(handles[1]);
    free(handles);
}

void __mxio_rchannel_init(void) {
    if (pthread_key_create(&rchannel_key, &rchannel_cleanup) != 0)
        abort();
}

static const char* _opnames[] = MXRIO_OPNAMES;
const char* mxio_opname(uint32_t op) {
    op = MXRIO_OPNAME(op);
    if (op < MXRIO_NUM_OPS) {
        return _opnames[op];
    } else {
        return "unknown";
    }
}

static bool is_message_valid(mxrio_msg_t* msg) {
    if ((msg->datalen > MXIO_CHUNK_SIZE) ||
        (msg->hcount > MXIO_MAX_HANDLES)) {
        return false;
    }
    return true;
}

static bool is_message_reply_valid(mxrio_msg_t* msg, uint32_t size) {
    if ((size < MXRIO_HDR_SZ) ||
        (msg->datalen != (size - MXRIO_HDR_SZ))) {
        return false;
    }
    return is_message_valid(msg);
}

static void discard_handles(mx_handle_t* handles, unsigned count) {
    while (count-- > 0) {
        mx_handle_close(*handles++);
    }
}

mx_status_t mxrio_handler(mx_handle_t h, void* _cb, void* cookie) {
    mxrio_cb_t cb = _cb;
    mxrio_msg_t msg;
    mx_status_t r;

    if (h == 0) {
        // remote side was closed;
        msg.op = MXRIO_CLOSE;
        msg.arg = 0;
        msg.datalen = 0;
        msg.hcount = 0;
        cb(&msg, 0, cookie);
        return NO_ERROR;
    }

    msg.hcount = MXIO_MAX_HANDLES;
    uint32_t dsz = sizeof(msg);
    if ((r = mx_channel_read(h, 0, &msg, dsz, &dsz, msg.handle, msg.hcount, &msg.hcount)) < 0) {
        if (r == ERR_SHOULD_WAIT) {
            return ERR_DISPATCHER_NO_WORK;
        }
        return r;
    }

    if (!is_message_reply_valid(&msg, dsz)) {
        discard_handles(msg.handle, msg.hcount);
        return ERR_INVALID_ARGS;
    }

    bool is_close = (MXRIO_OP(msg.op) == MXRIO_CLOSE);

    xprintf("handle_rio: op=%s arg=%d len=%u hsz=%d\n",
            mxio_opname(msg.op), msg.arg, msg.datalen, msg.hcount);

    if ((msg.arg = cb(&msg, h, cookie)) == ERR_DISPATCHER_INDIRECT) {
        // callback is handling the reply itself
        // and took ownership of the reply handle
        return 0;
    }
    if ((msg.arg < 0) || !is_message_valid(&msg)) {
        // in the event of an error response or bad message
        // release all the handles and data payload
        discard_handles(msg.handle, msg.hcount);
        msg.datalen = 0;
        msg.hcount = 0;
        // specific errors are prioritized over the bad
        // message case which we represent as ERR_INTERNAL
        // to differentiate from ERR_IO on the near side
        // TODO: consider a better error code
        msg.arg = (msg.arg < 0) ? msg.arg : ERR_INTERNAL;
    }

    msg.op = MXRIO_STATUS;
    if ((r = mx_channel_write(h, 0, &msg, MXRIO_HDR_SZ + msg.datalen, msg.handle, msg.hcount)) < 0) {
        discard_handles(msg.handle, msg.hcount);
    }
    if (is_close) {
        // signals to not perform a close callback
        return 1;
    } else {
        return r;
    }
}

void mxrio_txn_handoff(mx_handle_t srv, mx_handle_t reply, mxrio_msg_t* msg) {
    msg->txid = 0;
    msg->handle[0] = reply;
    msg->hcount = 1;

    mx_status_t r;
    uint32_t dsize = MXRIO_HDR_SZ + msg->datalen;
    if ((r = mx_channel_write(srv, 0, msg, dsize, msg->handle, msg->hcount)) < 0) {
        // nothing to do but inform the caller that we failed
        struct {
            mx_status_t status;
            uint32_t type;
        } error = { r, 0 };
        mx_channel_write(reply, 0, &error, sizeof(error), NULL, 0);
        mx_handle_close(reply);
    }
}

// on success, msg->hcount indicates number of valid handles in msg->handle
// on error there are never any handles
static mx_status_t mxrio_txn(mxrio_t* rio, mxrio_msg_t* msg) {
    if (!is_message_valid(msg)) {
        return ERR_INVALID_ARGS;
    }

    msg->txid = atomic_fetch_add(&rio->txid, 1);
    xprintf("txn h=%x txid=%x op=%d len=%u\n", rio->h, msg->txid, msg->op, msg->datalen);

    mx_status_t r;
    mx_status_t rs = ERR_INTERNAL;
    uint32_t dsize;

    mx_channel_call_args_t args;
    args.wr_bytes = msg;
    args.wr_handles = msg->handle;
    args.rd_bytes = msg;
    args.rd_handles = msg->handle;
    args.wr_num_bytes = MXRIO_HDR_SZ + msg->datalen;
    args.wr_num_handles = msg->hcount;
    args.rd_num_bytes = MXRIO_HDR_SZ + MXIO_CHUNK_SIZE;
    args.rd_num_handles = MXIO_MAX_HANDLES;

    r = mx_channel_call(rio->h, 0, MX_TIME_INFINITE, &args, &dsize, &msg->hcount, &rs);
    if (r < 0) {
        if (r == ERR_CALL_FAILED) {
            // read phase failed, true status is in rs
            msg->hcount = 0;
            return rs;
        } else {
            // write phase failed, we must discard the handles
            goto fail_discard_handles;
        }
    }

    // check for protocol errors
    if (!is_message_reply_valid(msg, dsize) ||
        (MXRIO_OP(msg->op) != MXRIO_STATUS)) {
        r = ERR_IO;
        goto fail_discard_handles;
    }
    // check for remote error
    if ((r = msg->arg) < 0) {
        goto fail_discard_handles;
    }
    return r;

fail_discard_handles:
    // We failed either writing at all (still have the handles)
    // or after reading (need to abandon any handles we received)
    discard_handles(msg->handle, msg->hcount);
    msg->hcount = 0;
    return r;
}

static ssize_t mxrio_ioctl(mxio_t* io, uint32_t op, const void* in_buf,
                           size_t in_len, void* out_buf, size_t out_len) {
    mxrio_t* rio = (mxrio_t*)io;
    const uint8_t* data = in_buf;
    mx_status_t r = 0;
    mxrio_msg_t msg;

    if (in_len > MXIO_IOCTL_MAX_INPUT || out_len > MXIO_CHUNK_SIZE) {
        return ERR_INVALID_ARGS;
    }

    memset(&msg, 0, MXRIO_HDR_SZ);
    msg.op = MXRIO_IOCTL;
    msg.datalen = in_len;
    msg.arg = out_len;
    msg.arg2.op = op;

    switch (IOCTL_KIND(op)) {
    case IOCTL_KIND_GET_HANDLE:
        if (out_len < sizeof(mx_handle_t)) {
            return ERR_INVALID_ARGS;
        }
        break;
    case IOCTL_KIND_GET_TWO_HANDLES:
        if (out_len < 2 * sizeof(mx_handle_t)) {
            return ERR_INVALID_ARGS;
        }
        break;
    case IOCTL_KIND_GET_THREE_HANDLES:
        if (out_len < 3 * sizeof(mx_handle_t)) {
            return ERR_INVALID_ARGS;
        }
        break;
    case IOCTL_KIND_SET_HANDLE:
        msg.op = MXRIO_IOCTL_1H;
        if (in_len < sizeof(mx_handle_t)) {
            return ERR_INVALID_ARGS;
        }
        msg.hcount = 1;
        msg.handle[0] = *((mx_handle_t*) in_buf);
        break;
    }

    memcpy(msg.data, data, in_len);

    if ((r = mxrio_txn(rio, &msg)) < 0) {
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
                memcpy(out_buf, msg.handle, sizeof(mx_handle_t));
            } else {
                memset(out_buf, 0, sizeof(mx_handle_t));
            }
            break;
        case IOCTL_KIND_GET_TWO_HANDLES:
            handles = (msg.hcount > 2 ? 2 : msg.hcount);
            if (handles) {
                memcpy(out_buf, msg.handle, handles * sizeof(mx_handle_t));
            }
            if (handles < 2) {
                memset(out_buf, 0, (2 - handles) * sizeof(mx_handle_t));
            }
            break;
        case IOCTL_KIND_GET_THREE_HANDLES:
            handles = (msg.hcount > 3 ? 3 : msg.hcount);
            if (handles) {
                memcpy(out_buf, msg.handle, handles * sizeof(mx_handle_t));
            }
            if (handles < 3) {
                memset(out_buf, 0, (3 - handles) * sizeof(mx_handle_t));
            }
            break;
    }
    discard_handles(msg.handle + handles, msg.hcount - handles);

    return r;
}

static ssize_t write_common(uint32_t op, mxio_t* io, const void* _data, size_t len, off_t offset) {
    mxrio_t* rio = (mxrio_t*)io;
    const uint8_t* data = _data;
    ssize_t count = 0;
    mx_status_t r = 0;
    mxrio_msg_t msg;
    ssize_t xfer;

    while (len > 0) {
        xfer = (len > MXIO_CHUNK_SIZE) ? MXIO_CHUNK_SIZE : len;

        memset(&msg, 0, MXRIO_HDR_SZ);
        msg.op = op;
        msg.datalen = xfer;
        if (op == MXRIO_WRITE_AT)
            msg.arg2.off = offset;
        memcpy(msg.data, data, xfer);

        if ((r = mxrio_txn(rio, &msg)) < 0) {
            break;
        }
        discard_handles(msg.handle, msg.hcount);

        if (r > xfer) {
            r = ERR_IO;
            break;
        }
        count += r;
        data += r;
        len -= r;
        if (op == MXRIO_WRITE_AT)
            offset += r;
        // stop at short read
        if (r < xfer) {
            break;
        }
    }
    return count ? count : r;
}

static ssize_t mxrio_write(mxio_t* io, const void* _data, size_t len) {
    return write_common(MXRIO_WRITE, io, _data, len, 0);
}

static ssize_t mxrio_write_at(mxio_t* io, const void* _data, size_t len, mx_off_t offset) {
    return write_common(MXRIO_WRITE_AT, io, _data, len, offset);
}

static ssize_t read_common(uint32_t op, mxio_t* io, void* _data, size_t len, off_t offset) {
    mxrio_t* rio = (mxrio_t*)io;
    uint8_t* data = _data;
    ssize_t count = 0;
    mx_status_t r = 0;
    mxrio_msg_t msg;
    ssize_t xfer;

    while (len > 0) {
        xfer = (len > MXIO_CHUNK_SIZE) ? MXIO_CHUNK_SIZE : len;

        memset(&msg, 0, MXRIO_HDR_SZ);
        msg.op = op;
        msg.arg = xfer;
        if (op == MXRIO_READ_AT)
            msg.arg2.off = offset;

        if ((r = mxrio_txn(rio, &msg)) < 0) {
            break;
        }
        discard_handles(msg.handle, msg.hcount);

        if ((r > (int)msg.datalen) || (r > xfer)) {
            r = ERR_IO;
            break;
        }
        memcpy(data, msg.data, r);
        count += r;
        data += r;
        len -= r;
        if (op == MXRIO_READ_AT)
            offset += r;

        // stop at short read
        if (r < xfer) {
            break;
        }
    }
    return count ? count : r;
}

static ssize_t mxrio_read(mxio_t* io, void* _data, size_t len) {
    return read_common(MXRIO_READ, io, _data, len, 0);
}

static ssize_t mxrio_read_at(mxio_t* io, void* _data, size_t len, mx_off_t offset) {
    return read_common(MXRIO_READ_AT, io, _data, len, offset);
}

static off_t mxrio_seek(mxio_t* io, off_t offset, int whence) {
    mxrio_t* rio = (mxrio_t*)io;
    mxrio_msg_t msg;
    mx_status_t r;

    memset(&msg, 0, MXRIO_HDR_SZ);
    msg.op = MXRIO_SEEK;
    msg.arg2.off = offset;
    msg.arg = whence;

    if ((r = mxrio_txn(rio, &msg)) < 0) {
        return r;
    }

    discard_handles(msg.handle, msg.hcount);
    return msg.arg2.off;
}

static mx_status_t mxrio_close(mxio_t* io) {
    mxrio_t* rio = (mxrio_t*)io;
    mxrio_msg_t msg;
    mx_status_t r;

    memset(&msg, 0, MXRIO_HDR_SZ);
    msg.op = MXRIO_CLOSE;

    if ((r = mxrio_txn(rio, &msg)) >= 0) {
        discard_handles(msg.handle, msg.hcount);
    }

    mx_handle_t h = rio->h;
    rio->h = 0;
    mx_handle_close(h);
    if (rio->h2 > 0) {
        h = rio->h2;
        rio->h2 = 0;
        mx_handle_close(h);
    }

    return r;
}

static mx_status_t mxrio_reply_channel_call(mxrio_t* rio, mxrio_msg_t* msg,
                                            mxrio_object_t* info) {
    mx_status_t r;
    mx_handle_t h;
    if ((r = mx_channel_create(0, &h, &msg->handle[0])) < 0) {
        return r;
    }
    msg->hcount = 1;

    // Write the (one-way) request message
    if ((r = mx_channel_write(rio->h, 0, msg, MXRIO_HDR_SZ + msg->datalen,
                              msg->handle, msg->hcount)) < 0) {
        mx_handle_close(msg->handle[0]);
        mx_handle_close(h);
        return r;
    }

    // Wait
    mx_object_wait_one(h, MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED, MX_TIME_INFINITE, NULL);

    // Attempt to read the callback response
    memset(info, 0xfe, sizeof(*info));
    uint32_t dsize = MXRIO_OBJECT_MAXSIZE;
    info->hcount = MXIO_MAX_HANDLES;
    r = mx_channel_read(h, 0, info, dsize, &dsize, info->handle,
                        info->hcount, &info->hcount);

    mx_handle_close(h);

    if (r < 0) {
        return r;
    }
    if (dsize < MXRIO_OBJECT_MINSIZE) {
        r = ERR_IO;
    } else {
        info->esize = dsize - MXRIO_OBJECT_MINSIZE;
        r = info->status;
    }
    if (r < 0) {
        discard_handles(info->handle, info->hcount);
    }
    return r;
}

static mx_status_t mxrio_misc(mxio_t* io, uint32_t op, int64_t off,
                              uint32_t maxreply, void* ptr, size_t len) {
    mxrio_t* rio = (mxrio_t*)io;
    mxrio_msg_t msg;
    mx_status_t r;

    if ((len > MXIO_CHUNK_SIZE) || (maxreply > MXIO_CHUNK_SIZE)) {
        return ERR_INVALID_ARGS;
    }

    memset(&msg, 0, MXRIO_HDR_SZ);
    msg.op = op;
    msg.arg = maxreply;
    msg.arg2.off = off;
    msg.datalen = len;
    if (ptr && len > 0) {
        memcpy(msg.data, ptr, len);
    }
    switch (op) {
    case MXRIO_RENAME:
    case MXRIO_LINK:
        // As a hack, 'Rename' and 'Link' take token handles through
        // the offset argument.
        msg.handle[0] = (mx_handle_t) off;
        msg.hcount = 1;
    }

    if ((r = mxrio_txn(rio, &msg)) < 0) {
        return r;
    }

    discard_handles(msg.handle, msg.hcount);
    if (msg.datalen > maxreply) {
        return ERR_IO;
    }
    if (ptr && msg.datalen > 0) {
        memcpy(ptr, msg.data, msg.datalen);
    }
    return r;
}

mx_status_t mxio_from_handles(uint32_t type, mx_handle_t* handles, int hcount,
                              void* extra, uint32_t esize, mxio_t** out) {
    mx_status_t r;
    mxio_t* io;
    switch (type) {
    case MXIO_PROTOCOL_REMOTE:
        if (hcount == 1) {
            io = mxio_remote_create(handles[0], 0);
            xprintf("rio (%x,%x) -> %p\n", handles[0], 0, io);
        } else if (hcount == 2) {
            io = mxio_remote_create(handles[0], handles[1]);
            xprintf("rio (%x,%x) -> %p\n", handles[0], handles[1], io);
        } else {
            r = ERR_INVALID_ARGS;
            break;
        }
        if (io == NULL) {
            r = ERR_NO_RESOURCES;
        } else {
            *out = io;
            return NO_ERROR;
        }
        break;
    case MXIO_PROTOCOL_PIPE:
        if (hcount != 1) {
            r = ERR_INVALID_ARGS;
        } else if ((*out = mxio_pipe_create(handles[0])) == NULL) {
            r = ERR_NO_RESOURCES;
        } else {
            return NO_ERROR;
        }
        break;
    case MXIO_PROTOCOL_VMOFILE: {
        mx_off_t* args = extra;
        if ((hcount != 1) || (esize != (sizeof(mx_off_t) * 2))) {
            r = ERR_INVALID_ARGS;
        } else if ((*out = mxio_vmofile_create(handles[0], args[0], args[1])) == NULL) {
            r = ERR_NO_RESOURCES;
        } else {
            return NO_ERROR;
        }
        break;
    }
    case MXIO_PROTOCOL_SOCKET: {
        if (hcount == 1) {
            io = mxio_socket_create(handles[0], 0);
        } else if (hcount == 2) {
            io = mxio_socket_create(handles[0], handles[1]);
        } else {
            r = ERR_INVALID_ARGS;
            break;
        }
        if (io == NULL) {
            r = ERR_NO_RESOURCES;
        } else {
            *out = io;
            return NO_ERROR;
        }
    }
    default:
        r = ERR_NOT_SUPPORTED;
    }
    discard_handles(handles, hcount);
    return r;
}

static mx_status_t mxrio_getobject(mxrio_t* rio, uint32_t op, const char* name,
                                   int32_t flags, uint32_t mode,
                                   mxrio_object_t* info) {

    if (name == NULL) {
        return ERR_INVALID_ARGS;
    }

    size_t len = strlen(name);
    if (len >= PATH_MAX) {
        return ERR_BAD_PATH;
    }

    mxrio_msg_t msg;
    memset(&msg, 0, MXRIO_HDR_SZ);
    msg.op = op;
    msg.datalen = len;
    msg.arg = flags;
    msg.arg2.mode = mode;
    memcpy(msg.data, name, len);

    return mxrio_reply_channel_call(rio, &msg, info);
}

static mx_status_t mxrio_open(mxio_t* io, const char* path, int32_t flags, uint32_t mode, mxio_t** out) {
    mxrio_t* rio = (void*)io;
    mxrio_object_t info;
    mx_status_t r = mxrio_getobject(rio, MXRIO_OPEN, path, flags, mode, &info);
    if (r < 0) {
        return r;
    }
    return mxio_from_handles(info.type, info.handle, info.hcount, info.extra, info.esize, out);
}

static mx_status_t mxrio_clone(mxio_t* io, mx_handle_t* handles, uint32_t* types) {
    mxrio_t* rio = (void*)io;
    mxrio_object_t info;
    mx_status_t r = mxrio_getobject(rio, MXRIO_CLONE, "", 0, 0, &info);
    if (r < 0) {
        return r;
    }
    for (unsigned i = 0; i < info.hcount; i++) {
        types[i] = MX_HND_TYPE_MXIO_REMOTE;
    }
    memcpy(handles, info.handle, info.hcount * sizeof(mx_handle_t));
    return info.hcount;
}

mx_status_t __mxrio_clone(mx_handle_t h, mx_handle_t* handles, uint32_t* types) {
    mxrio_t rio;
    rio.h = h;
    return mxrio_clone(&rio.io, handles, types);
}

static mx_status_t mxrio_unwrap(mxio_t* io, mx_handle_t* handles, uint32_t* types) {
    mxrio_t* rio = (void*)io;
    mx_status_t r;
    handles[0] = rio->h;
    types[0] = MX_HND_TYPE_MXIO_REMOTE;
    if (rio->h2 != 0) {
        handles[1] = rio->h2;
        types[1] = MX_HND_TYPE_MXIO_REMOTE;
        r = 2;
    } else {
        r = 1;
    }
    free(io);
    return r;
}

static void mxrio_wait_begin(mxio_t* io, uint32_t events, mx_handle_t* handle, mx_signals_t* _signals) {
    mxrio_t* rio = (void*)io;
    *handle = rio->h2;
    // POLLERR is always detected
    *_signals = ((EPOLLERR | events) & POLL_MASK) << POLL_SHIFT;
}

static void mxrio_wait_end(mxio_t* io, mx_signals_t signals, uint32_t* _events) {
    *_events = (signals >> POLL_SHIFT) & POLL_MASK;
}

static mxio_ops_t mx_remote_ops = {
    .read = mxrio_read,
    .read_at = mxrio_read_at,
    .write = mxrio_write,
    .write_at = mxrio_write_at,
    .recvmsg = mxio_default_recvmsg,
    .sendmsg = mxio_default_sendmsg,
    .misc = mxrio_misc,
    .seek = mxrio_seek,
    .close = mxrio_close,
    .open = mxrio_open,
    .clone = mxrio_clone,
    .ioctl = mxrio_ioctl,
    .wait_begin = mxrio_wait_begin,
    .wait_end = mxrio_wait_end,
    .unwrap = mxrio_unwrap,
    .posix_ioctl = mxio_default_posix_ioctl,
    .get_vmo = mxio_default_get_vmo,
};

mxio_t* mxio_remote_create(mx_handle_t h, mx_handle_t e) {
    mxrio_t* rio = calloc(1, sizeof(*rio));
    if (rio == NULL)
        return NULL;
    rio->io.ops = &mx_remote_ops;
    rio->io.magic = MXIO_MAGIC;
    atomic_init(&rio->io.refcount, 1);
    rio->h = h;
    rio->h2 = e;
    return &rio->io;
}

static ssize_t mxsio_read_stream(mxio_t* io, void* data, size_t len) {
    mxrio_t* rio = (mxrio_t*)io;
    int nonblock = rio->io.flags & MXIO_FLAG_NONBLOCK;

    // TODO: let the generic read() to do this loop
    for (;;) {
        ssize_t r;
        if ((r = mx_socket_read(rio->h2, 0, data, len, &len)) == NO_ERROR) {
            return (ssize_t) len;
        }
        if (r == ERR_PEER_CLOSED) {
            return 0;
        } else if (r == ERR_SHOULD_WAIT && !nonblock) {
            mx_signals_t pending;
            r = mx_object_wait_one(rio->h2,
                                   MX_SOCKET_READABLE | MX_SOCKET_PEER_CLOSED,
                                   MX_TIME_INFINITE, &pending);
            if (r < 0) {
                return r;
            }
            if (pending & MX_SOCKET_READABLE) {
                continue;
            }
            if (pending & MX_SOCKET_PEER_CLOSED) {
                return 0;
            }
            // impossible
            return ERR_INTERNAL;
        }
        return r;
    }
}

static ssize_t mxsio_write_stream(mxio_t* io, const void* data, size_t len) {
    mxrio_t* rio = (mxrio_t*)io;
    int nonblock = rio->io.flags & MXIO_FLAG_NONBLOCK;

    // TODO: let the generic write() to do this loop
    for (;;) {
        ssize_t r;
        if ((r = mx_socket_write(rio->h2, 0, data, len, &len)) == NO_ERROR) {
            return (ssize_t) len;
        }
        if (r == ERR_SHOULD_WAIT && !nonblock) {
            // No wait for PEER_CLOSED signal. PEER_CLOSED could be signaled
            // even if the socket is only half-closed for read.
            // TODO: how to detect if the write direction is closed?
            mx_signals_t pending;
            r = mx_object_wait_one(rio->h2,
                                   MX_SOCKET_WRITABLE,
                                   MX_TIME_INFINITE, &pending);
            if (r < 0) {
                return r;
            }
            if (pending & MX_SOCKET_WRITABLE) {
                continue;
            }
            // impossible
            return ERR_INTERNAL;
        }
        return r;
    }
}

static ssize_t mxsio_recvmsg_stream(mxio_t* io, struct msghdr* msg, int flags) {
    // TODO: support flags and control messages
    if (io->flags & MXIO_FLAG_SOCKET_CONNECTED) {
        // if connected, can't specify address
        if (msg->msg_name != NULL || msg->msg_namelen != 0) {
            return ERR_ALREADY_EXISTS;
        }
    } else {
        return ERR_BAD_STATE;
    }
    ssize_t total = 0;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        struct iovec *iov = &msg->msg_iov[i];
        ssize_t n = mxsio_read_stream(io, iov->iov_base, iov->iov_len);
        if (n < 0) {
            return n;
        }
        total += n;
        if ((size_t)n != iov->iov_len) {
            break;
        }
    }
    return total;
}

static ssize_t mxsio_sendmsg_stream(mxio_t* io, const struct msghdr* msg, int flags) {
    // TODO: support flags and control messages
    if (io->flags & MXIO_FLAG_SOCKET_CONNECTED) {
        // if connected, can't specify address
        if (msg->msg_name != NULL || msg->msg_namelen != 0) {
            return ERR_ALREADY_EXISTS;
        }
    } else {
        return ERR_BAD_STATE;
    }
    ssize_t total = 0;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        struct iovec *iov = &msg->msg_iov[i];
        if (iov->iov_len <= 0) {
            return ERR_INVALID_ARGS;
        }
        ssize_t n = mxsio_write_stream(io, iov->iov_base, iov->iov_len);
        if (n < 0) {
            return n;
        }
        total += n;
        if ((size_t)n != iov->iov_len) {
            break;
        }
    }
    return total;
}

static void mxsio_wait_begin_stream(mxio_t* io, uint32_t events, mx_handle_t* handle, mx_signals_t* _signals) {
    mxrio_t* rio = (void*)io;
    *handle = rio->h2;
    // TODO: locking for flags/state
    if (io->flags & MXIO_FLAG_SOCKET_CONNECTING) {
        // check the connection state
        mx_signals_t observed;
        mx_status_t r;
        r = mx_object_wait_one(rio->h2, MXSIO_SIGNAL_CONNECTED, 0u,
                               &observed);
        if (r == NO_ERROR || r == ERR_TIMED_OUT) {
            if (observed & MXSIO_SIGNAL_CONNECTED) {
                io->flags &= ~MXIO_FLAG_SOCKET_CONNECTING;
                io->flags |= MXIO_FLAG_SOCKET_CONNECTED;
            }
        }
    }
    mx_signals_t signals = MXSIO_SIGNAL_ERROR;
    if (io->flags & MXIO_FLAG_SOCKET_CONNECTED) {
        // if socket is connected
        if (events & EPOLLIN) {
            signals |= MX_SOCKET_READABLE | MX_SOCKET_PEER_CLOSED;
        }
        if (events & EPOLLOUT) {
            signals |= MX_SOCKET_WRITABLE;
        }
    } else {
        // if socket is not connected
        if (events & EPOLLIN) {
            // signal when a listening socket gets an incoming connection
            // or a connecting socket gets connected and receives data
            signals |= MXSIO_SIGNAL_INCOMING |
                MX_SOCKET_READABLE | MX_SOCKET_PEER_CLOSED;
        }
        if (events & EPOLLOUT) {
            // signal when connect() operation is finished
            signals |= MXSIO_SIGNAL_OUTGOING;
        }
    }
    if (events & EPOLLRDHUP) {
        signals |= MX_SOCKET_PEER_CLOSED;
    }
    *_signals = signals;
}

static void mxsio_wait_end_stream(mxio_t* io, mx_signals_t signals, uint32_t* _events) {
    // check the connection state
    if (io->flags & MXIO_FLAG_SOCKET_CONNECTING) {
        if (signals & MXSIO_SIGNAL_CONNECTED) {
            io->flags &= ~MXIO_FLAG_SOCKET_CONNECTING;
            io->flags |= MXIO_FLAG_SOCKET_CONNECTED;
        }
    }
    uint32_t events = 0;
    if (io->flags & MXIO_FLAG_SOCKET_CONNECTED) {
        if (signals & (MX_SOCKET_READABLE | MX_SOCKET_PEER_CLOSED)) {
            events |= EPOLLIN;
        }
        if (signals & MX_SOCKET_WRITABLE) {
            events |= EPOLLOUT;
        }
    } else {
        if (signals & (MXSIO_SIGNAL_INCOMING | MX_SOCKET_PEER_CLOSED)) {
            events |= EPOLLIN;
        }
        if (signals & MXSIO_SIGNAL_OUTGOING) {
            events |= EPOLLOUT;
        }
    }
    if (signals & MXSIO_SIGNAL_ERROR) {
        events |= EPOLLERR;
    }
    if (signals & MX_SOCKET_PEER_CLOSED) {
        events |= EPOLLRDHUP;
    }
    *_events = events;
}

static ssize_t mxsio_posix_ioctl_stream(mxio_t* io, int req, va_list va) {
    mxrio_t* rio = (mxrio_t*)io;
    switch (req) {
    case FIONREAD: {
        mx_status_t r;
        size_t avail;
        if ((r = mx_socket_read(rio->h2, 0, NULL, 0, &avail)) < 0) {
            return r;
        }
        if (avail > INT_MAX) {
            avail = INT_MAX;
        }
        int* actual = va_arg(va, int*);
        *actual = avail;
        return NO_ERROR;
    }
    default:
        return ERR_NOT_SUPPORTED;
    }
}

static ssize_t mxsio_rx_dgram(mxio_t* io, void* buf, size_t buflen) {
    size_t n = 0;
    for (;;) {
        ssize_t r;
        mxrio_t* rio = (mxrio_t*)io;
        // TODO: if mx_socket support dgram mode, we'll switch to it
        if ((r = mx_channel_read(rio->h2, 0, buf, buflen, (uint32_t*)&n,
                                 NULL, 0, NULL)) == NO_ERROR) {
            return n;
        }
        if (r == ERR_PEER_CLOSED) {
            return 0;
        } else if (r == ERR_SHOULD_WAIT) {
            if (io->flags & MXIO_FLAG_NONBLOCK) {
                return r;
            }
            mx_signals_t pending;
            r = mx_object_wait_one(rio->h2,
                                   MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED,
                                   MX_TIME_INFINITE, &pending);
            if (r < 0) {
                return r;
            }
            if (pending & MX_CHANNEL_READABLE) {
                continue;
            }
            if (pending & MX_CHANNEL_PEER_CLOSED) {
                return 0;
            }
            // impossible
            return ERR_INTERNAL;
        }
        return (ssize_t)n;
    }
}

static ssize_t mxsio_tx_dgram(mxio_t* io, const void* buf, size_t buflen) {
    mxrio_t* rio = (mxrio_t*)io;
    // TODO: mx_channel_write never returns ERR_SHOULD_WAIT, which is a problem.
    // if mx_socket supports dgram mode, we'll switch to it.
    return mx_channel_write(rio->h2, 0, buf, buflen, NULL, 0);
}

static ssize_t mxsio_recvmsg_dgram(mxio_t* io, struct msghdr* msg, int flags);
static ssize_t mxsio_sendmsg_dgram(mxio_t* io, const struct msghdr* msg, int flags);

static ssize_t mxsio_read_dgram(mxio_t* io, void* data, size_t len) {
    struct iovec iov;
    iov.iov_base = data;
    iov.iov_len = len;

    struct msghdr msg;
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;

    return mxsio_recvmsg_dgram(io, &msg, 0);
}

static ssize_t mxsio_write_dgram(mxio_t* io, const void* data, size_t len) {
    struct iovec iov;
    iov.iov_base = (void*)data;
    iov.iov_len = len;

    struct msghdr msg;
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;

    return mxsio_sendmsg_dgram(io, &msg, 0);
}

static ssize_t mxsio_recvmsg_dgram(mxio_t* io, struct msghdr* msg, int flags) {
    // TODO: support flags and control messages
    if (io->flags & MXIO_FLAG_SOCKET_CONNECTED) {
        // if connected, can't specify address
        if (msg->msg_name != NULL || msg->msg_namelen != 0) {
            return ERR_ALREADY_EXISTS;
        }
    }
    size_t mlen = 0;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        struct iovec *iov = &msg->msg_iov[i];
        if (iov->iov_len <= 0) {
            return ERR_INVALID_ARGS;
        }
        mlen += iov->iov_len;
    }
    mlen += MXIO_SOCKET_MSG_HEADER_SIZE;

    // TODO: avoid malloc
    mxio_socket_msg_t* m = malloc(mlen);
    ssize_t n = mxsio_rx_dgram(io, m, mlen);
    if (n < 0) {
        free(m);
        return n;
    }
    if ((size_t)n < MXIO_SOCKET_MSG_HEADER_SIZE) {
        free(m);
        return ERR_INTERNAL;
    }
    n -= MXIO_SOCKET_MSG_HEADER_SIZE;
    if (msg->msg_name != NULL) {
        memcpy(msg->msg_name, &m->addr, m->addrlen);
    }
    msg->msg_namelen = m->addrlen;
    msg->msg_flags = m->flags;
    char* data = m->data;
    size_t resid = n;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        struct iovec *iov = &msg->msg_iov[i];
        if (resid == 0) {
            iov->iov_len = 0;
        } else {
            if (resid < iov->iov_len)
                iov->iov_len = resid;
            memcpy(iov->iov_base, data, iov->iov_len);
            data += iov->iov_len;
            resid -= iov->iov_len;
        }
    }
    free(m);
    return n;
}

static ssize_t mxsio_sendmsg_dgram(mxio_t* io, const struct msghdr* msg, int flags) {
    // TODO: support flags and control messages
    if (io->flags & MXIO_FLAG_SOCKET_CONNECTED) {
        // if connected, can't specify address
        if (msg->msg_name != NULL || msg->msg_namelen != 0) {
            return ERR_ALREADY_EXISTS;
        }
    }
    ssize_t n = 0;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        struct iovec *iov = &msg->msg_iov[i];
        if (iov->iov_len <= 0) {
            return ERR_INVALID_ARGS;
        }
        n += iov->iov_len;
    }
    size_t mlen = n + MXIO_SOCKET_MSG_HEADER_SIZE;

    // TODO: avoid malloc m
    mxio_socket_msg_t* m = malloc(mlen);
    if (msg->msg_name != NULL) {
        memcpy(&m->addr, msg->msg_name, msg->msg_namelen);
    }
    m->addrlen = msg->msg_namelen;
    m->flags = flags;
    char* data = m->data;
    for (int i = 0; i < msg->msg_iovlen; i++) {
        struct iovec *iov = &msg->msg_iov[i];
        memcpy(data, iov->iov_base, iov->iov_len);
        data += iov->iov_len;
    }
    ssize_t r = mxsio_tx_dgram(io, m, mlen);
    free(m);
    return r == NO_ERROR ? n : r;
}

static void mxsio_wait_begin_dgram(mxio_t* io, uint32_t events, mx_handle_t* handle, mx_signals_t* _signals) {
    mxrio_t* rio = (void*)io;
    *handle = rio->h2;
    mx_signals_t signals = MXSIO_SIGNAL_ERROR;
    if (events & EPOLLIN) {
        signals |= MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
    }
    if (events & EPOLLOUT) {
        signals |= MX_CHANNEL_WRITABLE;
    }
    if (events & EPOLLRDHUP) {
        signals |= MX_CHANNEL_PEER_CLOSED;
    }
    *_signals = signals;
}

static void mxsio_wait_end_dgram(mxio_t* io, mx_signals_t signals, uint32_t* _events) {
    uint32_t events = 0;
    if (signals & (MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED)) {
        events |= EPOLLIN;
    }
    if (signals & MX_CHANNEL_WRITABLE) {
        events |= EPOLLOUT;
    }
    if (signals & MXSIO_SIGNAL_ERROR) {
        events |= EPOLLERR;
    }
    if (signals & MX_CHANNEL_PEER_CLOSED) {
        events |= EPOLLRDHUP;
    }
    *_events = events;
}

static mxio_ops_t mxio_socket_stream_ops = {
    .read = mxsio_read_stream,
    .write = mxsio_write_stream,
    .recvmsg = mxsio_recvmsg_stream,
    .sendmsg = mxsio_sendmsg_stream,
    .seek = mxio_default_seek,
    .misc = mxrio_misc,
    .close = mxrio_close,
    .open = mxrio_open,
    .clone = mxio_default_clone,
    .ioctl = mxrio_ioctl,
    .wait_begin = mxsio_wait_begin_stream,
    .wait_end = mxsio_wait_end_stream,
    .unwrap = mxio_default_unwrap,
    .posix_ioctl = mxsio_posix_ioctl_stream,
    .get_vmo = mxio_default_get_vmo,
};

static mxio_ops_t mxio_socket_dgram_ops = {
    .read = mxsio_read_dgram,
    .write = mxsio_write_dgram,
    .recvmsg = mxsio_recvmsg_dgram,
    .sendmsg = mxsio_sendmsg_dgram,
    .seek = mxio_default_seek,
    .misc = mxrio_misc,
    .close = mxrio_close,
    .open = mxrio_open,
    .clone = mxio_default_clone,
    .ioctl = mxrio_ioctl,
    .wait_begin = mxsio_wait_begin_dgram,
    .wait_end = mxsio_wait_end_dgram,
    .unwrap = mxio_default_unwrap,
    .posix_ioctl = mxio_default_posix_ioctl, // not supported
    .get_vmo = mxio_default_get_vmo,
};

mxio_t* mxio_socket_create(mx_handle_t h, mx_handle_t s) {
    mxrio_t* rio = calloc(1, sizeof(*rio));
    if (rio == NULL)
        return NULL;
    rio->io.ops = &mxio_socket_stream_ops; // default is stream
    rio->io.magic = MXIO_MAGIC;
    rio->io.refcount = 1;
    rio->io.flags |= MXIO_FLAG_SOCKET;
    rio->h = h;
    rio->h2 = s;
    return &rio->io;
}

void mxio_socket_set_stream_ops(mxio_t* io) {
    mxrio_t* rio = (mxrio_t*)io;
    rio->io.ops = &mxio_socket_stream_ops;
}

void mxio_socket_set_dgram_ops(mxio_t* io) {
    mxrio_t* rio = (mxrio_t*)io;
    rio->io.ops = &mxio_socket_dgram_ops;
}

mx_status_t mxio_socket_shutdown(mxio_t* io, int how) {
    mxrio_t* rio = (mxrio_t*)io;
    if (how == SHUT_RD || how == SHUT_RDWR) {
        // TODO: turn on a flag to prevent all read attempts
    }
    if (how == SHUT_WR || how == SHUT_RDWR) {
        // TODO: turn on a flag to prevent all write attempts
        mx_object_signal_peer(rio->h2, 0u, MXSIO_SIGNAL_HALFCLOSED);
    }
    return NO_ERROR;
}
