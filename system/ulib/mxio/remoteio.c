// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdatomic.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <threads.h>

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

typedef struct mxrio mxrio_t;
struct mxrio {
    // base mxio io object
    mxio_t io;

    // channel handle for rpc
    mx_handle_t h;

    // event handle for device state signals, or socket handle
    mx_handle_t h2;

    uint32_t flags;
};

static const char* _opnames[] = MXRIO_OPNAMES;
const char* mxio_opname(uint32_t op) {
    op = MXRIO_OP(op);
    if (op < MXRIO_NUM_OPS) {
        return _opnames[op];
    } else {
        return "unknown";
    }
}

static bool is_message_valid(mxrio_msg_t* msg) {
    if ((msg->magic != MXRIO_MAGIC) ||
        (msg->datalen > MXIO_CHUNK_SIZE) ||
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

    msg.hcount = MXIO_MAX_HANDLES + 1;
    uint32_t dsz = sizeof(msg);
    if ((r = mx_channel_read(h, 0, &msg, dsz, &dsz, msg.handle, msg.hcount, &msg.hcount)) < 0) {
        if (r == ERR_BAD_STATE) {
            return ERR_DISPATCHER_NO_WORK;
        }
        return r;
    }

    if (!is_message_reply_valid(&msg, dsz)) {
        discard_handles(msg.handle, msg.hcount);
        return ERR_INVALID_ARGS;
    }

    mx_handle_t rh = h;
    if (msg.op & MXRIO_REPLY_CHANNEL) {
        if (msg.hcount == 0) {
            discard_handles(msg.handle, msg.hcount);
            return ERR_INVALID_ARGS;
        }
        msg.hcount--;
        rh = msg.handle[msg.hcount];
    }

    bool is_close = (MXRIO_OP(msg.op) == MXRIO_CLOSE);

    xprintf("handle_rio: op=%s arg=%d len=%u hsz=%d\n",
            mxio_opname(msg.op), msg.arg, msg.datalen, msg.hcount);

    msg.arg = cb(&msg, (rh != h) ? rh : 0, cookie);
    if (msg.arg == ERR_DISPATCHER_INDIRECT) {
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

    // The kernel requires that a reply channel endpoint by
    // returned as the last handle attached to a write
    // to that endpoint
    if (rh != h) {
        msg.handle[msg.hcount++] = rh;
    }

    msg.op = MXRIO_STATUS;
    if ((r = mx_channel_write(rh, 0, &msg, MXRIO_HDR_SZ + msg.datalen, msg.handle, msg.hcount)) < 0) {
        discard_handles(msg.handle, msg.hcount);
    }
    if (is_close) {
        // signals to not perform a close callback
        return 1;
    } else {
        return r;
    }
}

mx_status_t mxrio_txn_handoff(mx_handle_t srv, mx_handle_t rh, mxrio_msg_t* msg) {
    msg->magic = MXRIO_MAGIC;
    msg->handle[0] = rh;
    msg->hcount = 1;
    msg->op |= MXRIO_REPLY_CHANNEL;

    if (!is_message_valid(msg)) {
        return ERR_INVALID_ARGS;
    }

    mx_status_t r;
    uint32_t dsize = MXRIO_HDR_SZ + msg->datalen;
    if ((r = mx_channel_write(srv, 0, msg, dsize, msg->handle, msg->hcount)) < 0) {
        return r;
    }
    return 0;
}

// on success, msg->hcount indicates number of valid handles in msg->handle
// on error there are never any handles
static mx_status_t mxrio_txn(mxrio_t* rio, mxrio_msg_t* msg) {
    msg->magic = MXRIO_MAGIC;
    if (!is_message_valid(msg)) {
        return ERR_INVALID_ARGS;
    }

    xprintf("txn h=%x op=%d len=%u\n", rio->h, msg->op, msg->datalen);
    uint32_t dsize = MXRIO_HDR_SZ + msg->datalen;

    mx_status_t r;

    static thread_local mx_handle_t *rchannel = NULL;
    if (rchannel == NULL) {
        if ((rchannel = malloc(sizeof(mx_handle_t) * 2)) == NULL) {
            return ERR_NO_MEMORY;
        }
        if ((r = mx_channel_create(MX_FLAG_REPLY_CHANNEL, &rchannel[0], &rchannel[1])) < 0) {
            free(rchannel);
            rchannel = NULL;
            return r;
        }
    }
    msg->op |= MXRIO_REPLY_CHANNEL;
    msg->handle[msg->hcount++] = rchannel[1];

    if ((r = mx_channel_write(rio->h, 0, msg, dsize, msg->handle, msg->hcount)) < 0) {
        msg->hcount--;
        goto fail_discard_handles;
    }

    mx_signals_t pending;
    if ((r = mx_handle_wait_one(rchannel[0], MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
                                MX_TIME_INFINITE, &pending)) < 0) {
        goto fail_close_reply_channel;
    }
    if ((pending & MX_SIGNAL_PEER_CLOSED) &&
        !(pending & MX_SIGNAL_READABLE)) {
        r = ERR_REMOTE_CLOSED;
        goto fail_close_reply_channel;
    }

    dsize = MXRIO_HDR_SZ + MXIO_CHUNK_SIZE;
    msg->hcount = MXIO_MAX_HANDLES + 1;
    if ((r = mx_channel_read(rchannel[0], 0, msg, dsize, &dsize,
                             msg->handle, msg->hcount, &msg->hcount)) < 0) {
        goto fail_close_reply_channel;
    }

    // The kernel ensures that the reply channel endpoint is
    // returned as the last handle in the message's handles.
    // The handle number may have changed, so update it.
    rchannel[1] = msg->handle[--msg->hcount];

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

fail_close_reply_channel:
    // We lost the far end of the reply channel, so
    // close the near end and try to replace it.
    // If that fails, free rchannel and let the next
    // txn try again.
    mx_handle_close(rchannel[0]);
    if (mx_channel_create(MX_FLAG_REPLY_CHANNEL, &rchannel[0], &rchannel[1]) < 0) {
        free(rchannel);
        rchannel = NULL;
    }
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

    if ((IOCTL_KIND(op) == IOCTL_KIND_GET_HANDLE) && (out_len < sizeof(mx_handle_t))) {
        return ERR_INVALID_ARGS;
    }
    if ((IOCTL_KIND(op) == IOCTL_KIND_GET_TWO_HANDLES) && (out_len < 2 * sizeof(mx_handle_t))) {
        return ERR_INVALID_ARGS;
    }

    memset(&msg, 0, MXRIO_HDR_SZ);
    msg.op = MXRIO_IOCTL;
    msg.datalen = in_len;
    msg.arg = out_len;
    msg.arg2.op = op;
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

static ssize_t mxrio_write_at(mxio_t* io, const void* _data, size_t len, off_t offset) {
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

static ssize_t mxrio_read_at(mxio_t* io, void* _data, size_t len, off_t offset) {
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
                                   mx_handle_t* handles, uint32_t* type,
                                   void* extra, size_t* esize) {
    if (name == NULL) {
        return ERR_INVALID_ARGS;
    }

    size_t len = strlen(name);
    if (len >= MXIO_CHUNK_SIZE) {
        return ERR_INVALID_ARGS;
    }

    mxrio_msg_t msg;
    memset(&msg, 0, MXRIO_HDR_SZ);
    msg.op = op;
    msg.datalen = len;
    msg.arg = flags;
    msg.arg2.mode = mode;
    memcpy(msg.data, name, len);

    mx_status_t r;
    if ((r = mxrio_txn(rio, &msg)) < 0) {
        return r;
    }
    if (msg.datalen) {
        if ((extra == NULL) || (*esize < msg.datalen)) {
            discard_handles(msg.handle, msg.hcount);
            return ERR_IO;
        }
        memcpy(extra, msg.data, msg.datalen);
    }
    if (esize) {
        *esize = msg.datalen;
    }
    memcpy(handles, msg.handle, msg.hcount * sizeof(mx_handle_t));
    *type = msg.arg2.protocol;
    return (mx_status_t)msg.hcount;
}

static mx_status_t mxrio_open(mxio_t* io, const char* path, int32_t flags, uint32_t mode, mxio_t** out) {
    mxrio_t* rio = (void*)io;
    mx_handle_t handles[MXIO_MAX_HANDLES];
    uint32_t type;
    uint8_t extra[16];
    size_t esize = sizeof(extra);
    mx_status_t r = mxrio_getobject(rio, MXRIO_OPEN, path, flags, mode, handles, &type, extra, &esize);
    if (r > 0) {
        r = mxio_from_handles(type, handles, r, extra, esize, out);
    }
    return r;
}

static mx_status_t mxrio_clone(mxio_t* io, mx_handle_t* handles, uint32_t* types) {
    mxrio_t* rio = (void*)io;
    mx_status_t r = mxrio_getobject(rio, MXRIO_CLONE, "", 0, 0, handles, types, NULL, NULL);
    for (int i = 0; i < r; i++) {
        types[i] = MX_HND_TYPE_MXIO_REMOTE;
    }
    return r;
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
    mx_signals_t signals = MX_USER_SIGNAL_2; // EPOLLERR is always detected
    if (events & EPOLLIN) {
        signals |= MX_USER_SIGNAL_0 | MX_SIGNAL_PEER_CLOSED;
    }
    if (events & EPOLLOUT) {
        signals |= MX_USER_SIGNAL_1;
    }
    if (events & EPOLLRDHUP) {
        signals |= MX_SIGNAL_PEER_CLOSED;
    }
    *_signals = signals;
}

static void mxrio_wait_end(mxio_t* io, mx_signals_t signals, uint32_t* _events) {
    uint32_t events = 0;
    if (signals & (MX_USER_SIGNAL_0 | MX_SIGNAL_PEER_CLOSED)) {
        events |= EPOLLIN;
    }
    if (signals & MX_USER_SIGNAL_1) {
        events |= EPOLLOUT;
    }
    if (signals & MX_USER_SIGNAL_2) {
        events |= EPOLLERR;
    }
    if (signals & MX_SIGNAL_PEER_CLOSED) {
        events |= EPOLLRDHUP;
    }
    *_events = events;
}

static mxio_ops_t mx_remote_ops = {
    .read = mxrio_read,
    .read_at = mxrio_read_at,
    .write = mxrio_write,
    .write_at = mxrio_write_at,
    .misc = mxrio_misc,
    .seek = mxrio_seek,
    .close = mxrio_close,
    .open = mxrio_open,
    .clone = mxrio_clone,
    .ioctl = mxrio_ioctl,
    .wait_begin = mxrio_wait_begin,
    .wait_end = mxrio_wait_end,
    .unwrap = mxrio_unwrap,
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

static ssize_t mxsio_read(mxio_t* io, void* data, size_t len) {
    mxrio_t* rio = (mxrio_t*)io;
    int nonblock = rio->io.flags & MXIO_FLAG_NONBLOCK;

    // TODO: let the generic read() to do this loop
    for (;;) {
        ssize_t r;
        if ((r = mx_socket_read(rio->h2, 0, data, len, &len)) == NO_ERROR) {
            return (ssize_t) len;
        }
        if (r == ERR_REMOTE_CLOSED) {
            return 0;
        } else if (r == ERR_SHOULD_WAIT && !nonblock) {
            mx_signals_t pending;
            r = mx_handle_wait_one(rio->h2,
                                   MX_SIGNAL_READABLE
                                   | MX_SIGNAL_PEER_CLOSED,
                                   MX_TIME_INFINITE, &pending);
            if (r < 0) {
                return r;
            }
            if (pending & MX_SIGNAL_READABLE) {
                continue;
            }
            if (pending & MX_SIGNAL_PEER_CLOSED) {
                return 0;
            }
            // impossible
            return ERR_INTERNAL;
        }
        return r;
    }
}

static ssize_t mxsio_write(mxio_t* io, const void* data, size_t len) {
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
            r = mx_handle_wait_one(rio->h2,
                                   MX_SIGNAL_WRITABLE,
                                   MX_TIME_INFINITE, &pending);
            if (r < 0) {
                return r;
            }
            if (pending & MX_SIGNAL_WRITABLE) {
                continue;
            }
            // impossible
            return ERR_INTERNAL;
        }
        return r;
    }
}

static void mxsio_wait_begin(mxio_t* io, uint32_t events, mx_handle_t* handle, mx_signals_t* _signals) {
    mxrio_t* rio = (void*)io;
    *handle = rio->h2;
    // TODO: locking for flags/state
    if (io->flags & MXIO_FLAG_SOCKET_CONNECTING) {
        // check the connection state
        mx_signals_t observed;
        mx_status_t r;
        r = mx_handle_wait_one(rio->h2, MXSIO_SIGNAL_CONNECTED, 0u,
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
            signals |= MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED;
        }
        if (events & EPOLLOUT) {
            signals |= MX_SIGNAL_WRITABLE;
        }
    } else {
        // if socket is not connected
        if (events & EPOLLIN) {
            // signal when a listening socket gets an incoming connection
            // or a connecting socket gets connected and receives data
            signals |= MXSIO_SIGNAL_INCOMING |
                MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED;
        }
        if (events & EPOLLOUT) {
            // signal when connect() operation is finished
            signals |= MXSIO_SIGNAL_OUTGOING;
        }
    }
    if (events & EPOLLRDHUP) {
        signals |= MX_SIGNAL_PEER_CLOSED;
    }
    *_signals = signals;
}

static void mxsio_wait_end(mxio_t* io, mx_signals_t signals, uint32_t* _events) {
    // check the connection state
    if (io->flags & MXIO_FLAG_SOCKET_CONNECTING) {
        if (signals & MXSIO_SIGNAL_CONNECTED) {
            io->flags &= ~MXIO_FLAG_SOCKET_CONNECTING;
            io->flags |= MXIO_FLAG_SOCKET_CONNECTED;
        }
    }
    uint32_t events = 0;
    if (io->flags & MXIO_FLAG_SOCKET_CONNECTED) {
        if (signals & (MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED)) {
            events |= EPOLLIN;
        }
        if (signals & MX_SIGNAL_WRITABLE) {
            events |= EPOLLOUT;
        }
    } else {
        if (signals & (MXSIO_SIGNAL_INCOMING | MX_SIGNAL_PEER_CLOSED)) {
            events |= EPOLLIN;
        }
        if (signals & MXSIO_SIGNAL_OUTGOING) {
            events |= EPOLLOUT;
        }
    }
    if (signals & MXSIO_SIGNAL_ERROR) {
        events |= EPOLLERR;
    }
    if (signals & MX_SIGNAL_PEER_CLOSED) {
        events |= EPOLLRDHUP;
    }
    *_events = events;
}

static mxio_ops_t mxio_socket_ops = {
    .read = mxsio_read,
    .write = mxsio_write,
    .seek = mxio_default_seek,
    .misc = mxrio_misc,
    .close = mxrio_close,
    .open = mxrio_open,
    .clone = mxio_default_clone,
    .ioctl = mxrio_ioctl,
    .wait_begin = mxsio_wait_begin,
    .wait_end = mxsio_wait_end,
    .unwrap = mxio_default_unwrap,
};

mxio_t* mxio_socket_create(mx_handle_t h, mx_handle_t s) {
    mxrio_t* rio = calloc(1, sizeof(*rio));
    if (rio == NULL)
        return NULL;
    rio->io.ops = &mxio_socket_ops;
    rio->io.magic = MXIO_MAGIC;
    rio->io.refcount = 1;
    rio->io.flags |= MXIO_FLAG_SOCKET;
    rio->h = h;
    rio->h2 = s;
    return &rio->io;
}

mx_status_t mxio_socket_posix_ioctl(mxio_t* io, int req, void* arg) {
    mxrio_t* rio = (mxrio_t*)io;
    switch (req) {
    case FIONREAD: {
        mx_status_t r;
        size_t avail;
        if ((r = mx_socket_read(rio->h2, 0, NULL, 0, &avail)) < 0) {
            return r;
        }
        *(int*)arg = avail;
        return NO_ERROR;
    }
    default:
        return ERR_NOT_SUPPORTED;
    }
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
