// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mxio/debug.h>
#include <mxio/dispatcher.h>
#include <mxio/io.h>
#include <mxio/remoteio.h>
#include <mxio/util.h>

#include <runtime/thread.h>
#include <runtime/mutex.h>

#include "private.h"

#define MXDEBUG 0

#define WITH_REPLY_PIPE 1

typedef struct mx_rio mx_rio_t;
struct mx_rio {
    // base mxio io object
    mxio_t io;

    // message pipe handle for rpc
    mx_handle_t h;

    // event handle for device state signals
    mx_handle_t e;

    uint32_t flags;

    // TODO: replace with reply-pipes to allow
    // true multithreaded io
    mxr_mutex_t lock;
};

static const char* _opnames[] = MX_RIO_OPNAMES;
static const char* opname(uint32_t op) {
    op = MX_RIO_OP(op);
    if (op < MX_RIO_NUM_OPS) {
        return _opnames[op];
    } else {
        return "unknown";
    }
}

static bool is_message_valid(mx_rio_msg_t* msg) {
    if ((msg->magic != MX_RIO_MAGIC) ||
        (msg->datalen > MXIO_CHUNK_SIZE) ||
        (msg->hcount > MXIO_MAX_HANDLES)) {
        return false;
    }
    return true;
}

static bool is_message_reply_valid(mx_rio_msg_t* msg, uint32_t size) {
    if ((size < MX_RIO_HDR_SZ) ||
        (msg->datalen != (size - MX_RIO_HDR_SZ))) {
        return false;
    }
    return is_message_valid(msg);
}

static void discard_handles(mx_handle_t* handles, unsigned count) {
    while (count-- > 0) {
        mx_handle_close(*handles++);
    }
}

mx_status_t mxio_rio_handler(mx_handle_t h, void* _cb, void* cookie) {
    mxio_rio_cb_t cb = _cb;
    mx_rio_msg_t msg;
    mx_status_t r;

    if (h == 0) {
        // remote side was closed;
        msg.op = MX_RIO_CLOSE;
        msg.arg = 0;
        msg.datalen = 0;
        msg.hcount = 0;
        cb(&msg, 0, cookie);
        return NO_ERROR;
    }

    msg.hcount = MXIO_MAX_HANDLES + 1;
    uint32_t dsz = sizeof(msg);
    if ((r = mx_message_read(h, &msg, &dsz, msg.handle, &msg.hcount, 0)) < 0) {
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
#if WITH_REPLY_PIPE
    if (msg.op & MX_RIO_REPLY_PIPE) {
        if (msg.hcount == 0) {
            discard_handles(msg.handle, msg.hcount);
            return ERR_INVALID_ARGS;
        }
        msg.hcount--;
        rh = msg.handle[msg.hcount];
    }
#endif

    bool is_close = (MX_RIO_OP(msg.op) == MX_RIO_CLOSE);

    xprintf("handle_rio: op=%s arg=%d len=%u hsz=%d\n",
            opname(msg.op), msg.arg, msg.datalen, msg.hcount);

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
        // message case which we represent as ERR_FAULT
        // to differentiate from ERR_IO on the near side
        // TODO: consider a better error code
        msg.arg = (msg.arg < 0) ? msg.arg : ERR_FAULT;
    }

#if WITH_REPLY_PIPE
    // The kernel requires that a reply pipe endpoint by
    // returned as the last handle attached to a write
    // to that endpoint
    if (rh != h) {
        msg.handle[msg.hcount++] = rh;
    }
#endif
    msg.op = MX_RIO_STATUS;
    if ((r = mx_message_write(rh, &msg, MX_RIO_HDR_SZ + msg.datalen, msg.handle, msg.hcount, 0)) < 0) {
        discard_handles(msg.handle, msg.hcount);
    }
    if (is_close) {
        // signals to not perform a close callback
        return 1;
    } else {
        return r;
    }
}

void mxio_rio_server(mx_handle_t h, mxio_rio_cb_t cb, void* cookie) {
    mx_signals_state_t pending;
    mx_status_t r;

    xprintf("riosvr(%x) starting...\n", h);
    for (;;) {
        r = mx_handle_wait_one(h, MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
                                     MX_TIME_INFINITE, &pending);
        if (r < 0)
            break;
        if (pending.satisfied & MX_SIGNAL_READABLE) {
            if ((r = mxio_rio_handler(h, cb, cookie)) != 0) {
                break;
            }
        }
        if (pending.satisfied & MX_SIGNAL_PEER_CLOSED) {
            r = ERR_CHANNEL_CLOSED;
            break;
        }
    }
    if (r < 0) {
        mxio_rio_handler(0, cb, cookie);
    }
    if (r != 0) {
        xprintf("riosvr(%x) done, status=%d\n", h, r);
    }
    mx_handle_close(h);
}

mx_status_t mx_rio_txn_handoff(mx_handle_t srv, mx_handle_t rh, mx_rio_msg_t* msg) {
    msg->magic = MX_RIO_MAGIC;
    msg->handle[0] = rh;
    msg->hcount = 1;
    msg->op |= MX_RIO_REPLY_PIPE;

    if (!is_message_valid(msg)) {
        return ERR_INVALID_ARGS;
    }

    mx_status_t r;
    uint32_t dsize = MX_RIO_HDR_SZ + msg->datalen;
    if ((r = mx_message_write(srv, msg, dsize, msg->handle, msg->hcount, 0)) < 0) {
        return r;
    }
    return 0;
}

#if WITH_REPLY_PIPE
#define mx_rio_txn_locked mx_rio_txn
#endif

// on success, msg->hcount indicates number of valid handles in msg->handle
// on error there are never any handles
static mx_status_t mx_rio_txn_locked(mx_rio_t* rio, mx_rio_msg_t* msg) {
    msg->magic = MX_RIO_MAGIC;
    if (!is_message_valid(msg)) {
        return ERR_INVALID_ARGS;
    }

    xprintf("txn h=%x op=%d len=%u\n", rio->h, msg->op, msg->datalen);
    uint32_t dsize = MX_RIO_HDR_SZ + msg->datalen;

    mx_status_t r;

#if WITH_REPLY_PIPE
    mx_handle_t rpipe[2];
    if ((r = mx_message_pipe_create(rpipe, MX_FLAG_REPLY_PIPE)) < 0) {
        return r;
    }
    msg->op |= MX_RIO_REPLY_PIPE;
    msg->handle[msg->hcount++] = rpipe[1];
    mx_handle_t rh = rpipe[0];
#else
    mx_handle_t rh = rio->h;
#endif

    if ((r = mx_message_write(rio->h, msg, dsize, msg->handle, msg->hcount, 0)) < 0) {
        goto fail_discard_handles;
    }

    mx_signals_state_t pending;
    if ((r = mx_handle_wait_one(rh, MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
                                      MX_TIME_INFINITE, &pending)) < 0) {
        return r;
    }
    if ((pending.satisfied & MX_SIGNAL_PEER_CLOSED) &&
        !(pending.satisfied & MX_SIGNAL_READABLE)) {
        mx_handle_close(rh);
        return ERR_CHANNEL_CLOSED;
    }

    dsize = MX_RIO_HDR_SZ + MXIO_CHUNK_SIZE;
    msg->hcount = MXIO_MAX_HANDLES + 1;
    if ((r = mx_message_read(rh, msg, &dsize, msg->handle, &msg->hcount, 0)) < 0) {
        goto done;
    }
#if WITH_REPLY_PIPE
    // the kernel ensures that the reply pipe endpoint is
    // returned as the last handle in the attached handles
    msg->hcount--;
    mx_handle_close(msg->handle[msg->hcount]);
#endif
    // check for protocol errors
    if (!is_message_reply_valid(msg, dsize) ||
        (MX_RIO_OP(msg->op) != MX_RIO_STATUS)) {
        r = ERR_IO;
        goto fail_discard_handles;
    }
    // check for remote error
    if ((r = msg->arg) >= 0) {
        goto done;
    }

fail_discard_handles:
    discard_handles(msg->handle, msg->hcount);
    msg->hcount = 0;
done:
#if WITH_REPLY_PIPE
    mx_handle_close(rh);
#endif
    return r;
}

#if WITH_REPLY_PIPE
#undef mx_rio_txn_locked
#else
static mx_status_t mx_rio_txn(mx_rio_t* rio, mx_rio_msg_t* msg) {
    mx_status_t r;
    mxr_mutex_lock(&rio->lock);
    r = mx_rio_txn_locked(rio, msg);
    mxr_mutex_unlock(&rio->lock);
    return r;
}
#endif

static ssize_t mx_rio_ioctl(mxio_t* io, uint32_t op, const void* in_buf,
                            size_t in_len, void* out_buf, size_t out_len) {
    mx_rio_t* rio = (mx_rio_t*)io;
    const uint8_t* data = in_buf;
    mx_status_t r = 0;
    mx_rio_msg_t msg;

    if (in_len > MXIO_IOCTL_MAX_INPUT || out_len > MXIO_CHUNK_SIZE) {
        return ERR_INVALID_ARGS;
    }

    if ((op == IOCTL_DEVICE_GET_HANDLE) && (out_len < sizeof(mx_handle_t))) {
        return ERR_INVALID_ARGS;
    }

    memset(&msg, 0, MX_RIO_HDR_SZ);
    msg.op = MX_RIO_IOCTL;
    msg.datalen = in_len;
    msg.arg = out_len;
    msg.arg2.op = op;
    memcpy(msg.data, data, in_len);

    if ((r = mx_rio_txn(rio, &msg)) < 0) {
        return r;
    }

    size_t copy_len = msg.datalen;
    if (msg.datalen > out_len) {
        copy_len = out_len;
    }

    memcpy(out_buf, msg.data, copy_len);
    if (op == IOCTL_DEVICE_GET_HANDLE) {
        if (msg.hcount > 0) {
            memcpy(out_buf, msg.handle, sizeof(mx_handle_t));
            discard_handles(msg.handle + 1, msg.hcount - 1);
        } else {
            mx_handle_t zero = 0;
            memcpy(out_buf, &zero, sizeof(mx_handle_t));
        }
    } else {
        discard_handles(msg.handle, msg.hcount);
    }
    return r;
}

static ssize_t mx_rio_write(mxio_t* io, const void* _data, size_t len) {
    mx_rio_t* rio = (mx_rio_t*)io;
    const uint8_t* data = _data;
    ssize_t count = 0;
    mx_status_t r = 0;
    mx_rio_msg_t msg;
    ssize_t xfer;

    while (len > 0) {
        xfer = (len > MXIO_CHUNK_SIZE) ? MXIO_CHUNK_SIZE : len;

        memset(&msg, 0, MX_RIO_HDR_SZ);
        msg.op = MX_RIO_WRITE;
        msg.datalen = xfer;
        memcpy(msg.data, data, xfer);

        if ((r = mx_rio_txn(rio, &msg)) < 0) {
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
        // stop at short read
        if (r < xfer) {
            break;
        }
    }
    return count ? count : r;
}

static ssize_t mx_rio_read(mxio_t* io, void* _data, size_t len) {
    mx_rio_t* rio = (mx_rio_t*)io;
    uint8_t* data = _data;
    ssize_t count = 0;
    mx_status_t r = 0;
    mx_rio_msg_t msg;
    ssize_t xfer;

    while (len > 0) {
        xfer = (len > MXIO_CHUNK_SIZE) ? MXIO_CHUNK_SIZE : len;

        memset(&msg, 0, MX_RIO_HDR_SZ);
        msg.op = MX_RIO_READ;
        msg.arg = xfer;

        if ((r = mx_rio_txn(rio, &msg)) < 0) {
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

        // stop at short read
        if (r < xfer) {
            break;
        }
    }
    return count ? count : r;
}

static off_t mx_rio_seek(mxio_t* io, off_t offset, int whence) {
    mx_rio_t* rio = (mx_rio_t*)io;
    mx_rio_msg_t msg;
    mx_status_t r;

    memset(&msg, 0, MX_RIO_HDR_SZ);
    msg.op = MX_RIO_SEEK;
    msg.arg2.off = offset;
    msg.arg = whence;

    if ((r = mx_rio_txn(rio, &msg)) < 0) {
        return r;
    }

    discard_handles(msg.handle, msg.hcount);
    return msg.arg2.off;
}

static mx_status_t mx_rio_close(mxio_t* io) {
    mx_rio_t* rio = (mx_rio_t*)io;
    mx_rio_msg_t msg;
    mx_status_t r;

    memset(&msg, 0, MX_RIO_HDR_SZ);
    msg.op = MX_RIO_CLOSE;

    if ((r = mx_rio_txn(rio, &msg)) >= 0) {
        discard_handles(msg.handle, msg.hcount);
    }

    mx_handle_t h = rio->h;
    rio->h = 0;
    mx_handle_close(h);
    if (rio->e > 0) {
        h = rio->e;
        rio->e = 0;
        mx_handle_close(h);
    }

    return r;
}

static mx_status_t mx_rio_misc(mxio_t* io, uint32_t op, uint32_t maxreply, void* ptr, size_t len) {
    mx_rio_t* rio = (mx_rio_t*)io;
    mx_rio_msg_t msg;
    mx_status_t r;

    if ((len > MXIO_CHUNK_SIZE) || (maxreply > MXIO_CHUNK_SIZE)) {
        return ERR_INVALID_ARGS;
    }

    memset(&msg, 0, MX_RIO_HDR_SZ);
    msg.op = op;
    msg.arg = maxreply;
    msg.datalen = len;
    memcpy(msg.data, ptr, len);

    if ((r = mx_rio_txn(rio, &msg)) < 0) {
        return r;
    }

    discard_handles(msg.handle, msg.hcount);
    if (r > (int)maxreply) {
        return ERR_IO;
    }
    memcpy(ptr, msg.data, r);
    return r;
}

mx_status_t mxio_from_handles(uint32_t type, mx_handle_t* handles, int hcount, mxio_t** out) {
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
    default:
        r = ERR_NOT_SUPPORTED;
    }
    discard_handles(handles, hcount);
    return r;
}

static mx_status_t mx_rio_getobject(mx_rio_t* rio, uint32_t op, const char* name,
                                    int32_t flags, mx_handle_t* handles, uint32_t* type) {
    if (name == NULL) {
        return ERR_INVALID_ARGS;
    }

    size_t len = strlen(name);
    if (len >= MXIO_CHUNK_SIZE) {
        return ERR_INVALID_ARGS;
    }

    mx_rio_msg_t msg;
    memset(&msg, 0, MX_RIO_HDR_SZ);
    msg.op = op;
    msg.datalen = len;
    msg.arg = flags;
    memcpy(msg.data, name, len);

    mx_status_t r;
    if ((r = mx_rio_txn(rio, &msg)) < 0) {
        return r;
    }
    memcpy(handles, msg.handle, msg.hcount * sizeof(mx_handle_t));
    *type = msg.arg2.protocol;
    return (mx_status_t)msg.hcount;
}

static mx_status_t mx_rio_open(mxio_t* io, const char* path, int32_t flags, mxio_t** out) {
    mx_rio_t* rio = (void*)io;
    mx_handle_t handles[MXIO_MAX_HANDLES];
    uint32_t type;
    mx_status_t r = mx_rio_getobject(rio, MX_RIO_OPEN, path, flags, handles, &type);
    if (r > 0) {
        r = mxio_from_handles(type, handles, r, out);
    }
    return r;
}

static mx_status_t mx_rio_clone(mxio_t* io, mx_handle_t* handles, uint32_t* types) {
    mx_rio_t* rio = (void*)io;
    mx_status_t r = mx_rio_getobject(rio, MX_RIO_CLONE, "", 0, handles, types);
    for (int i = 0; i < r; i++) {
        types[i] = MX_HND_TYPE_MXIO_REMOTE;
    }
    return r;
}

mx_status_t __mx_rio_clone(mx_handle_t h, mx_handle_t* handles, uint32_t* types) {
    mx_rio_t rio;
    rio.h = h;
    return mx_rio_clone(&rio.io, handles, types);
}

static mx_status_t mx_rio_wait(mxio_t* io, uint32_t events, uint32_t* _pending, mx_time_t timeout) {
    mx_rio_t* rio = (void*)io;
    if (rio->e == 0) {
        return ERR_NOT_SUPPORTED;
    }
    mx_status_t r;
    mx_signals_state_t pending;
    if ((r = mx_handle_wait_one(rio->e, events & MXIO_EVT_ALL, timeout, &pending)) < 0) {
        return r;
    }
    if (_pending) {
        *_pending = pending.satisfied;
    }
    return NO_ERROR;
}

static mxio_ops_t mx_remote_ops = {
    .read = mx_rio_read,
    .write = mx_rio_write,
    .misc = mx_rio_misc,
    .seek = mx_rio_seek,
    .close = mx_rio_close,
    .open = mx_rio_open,
    .clone = mx_rio_clone,
    .wait = mx_rio_wait,
    .ioctl = mx_rio_ioctl,
};

mxio_t* mxio_remote_create(mx_handle_t h, mx_handle_t e) {
    mx_rio_t* rio = calloc(1, sizeof(*rio));
    if (rio == NULL)
        return NULL;
    rio->io.ops = &mx_remote_ops;
    rio->io.magic = MXIO_MAGIC;
    rio->io.refcount = 1;
    rio->h = h;
    rio->e = e;
    rio->lock = MXR_MUTEX_INIT;
    return &rio->io;
}

typedef struct {
    mx_handle_t h;
    void* cb;
    void* cookie;
} rio_args_t;

static int rio_handler_thread(void* _args) {
    rio_args_t* args = (rio_args_t*)_args;
    mxio_rio_server(args->h, args->cb, args->cookie);
    return 0;
}

mx_status_t mxio_handler_create(mx_handle_t h, mxio_rio_cb_t cb, void* cookie) {
    rio_args_t* args;
    mxr_thread_t* t;
    if ((args = malloc(sizeof(*args))) == NULL) {
        goto fail;
    }
    args->h = h;
    args->cb = cb;
    args->cookie = cookie;
    if (mxr_thread_create(rio_handler_thread, args, "rio-handler", &t) < 0) {
        goto fail;
    }
    mxr_thread_detach(t);
    return 0;
fail:
    xprintf("riosvr: could not install handler %x %p\n", h, cb);
    mx_handle_close(h);
    free(args);
    return ERR_NO_RESOURCES;
}
