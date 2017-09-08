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

#include <magenta/device/device.h>
#include <magenta/device/ioctl.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>

#include <mxio/debug.h>
#include <mxio/io.h>
#include <mxio/namespace.h>
#include <mxio/remoteio.h>
#include <mxio/util.h>

#include "private-remoteio.h"

#define MXDEBUG 0

// POLL_MASK and POLL_SHIFT intend to convert the lower five POLL events into
// MX_USER_SIGNALs and vice-versa. Other events need to be manually converted to
// an mx_signal_t, if they are desired.
#define POLL_SHIFT  24
#define POLL_MASK   0x1F

static_assert(MX_USER_SIGNAL_0 == (1 << POLL_SHIFT), "");
static_assert((POLLIN << POLL_SHIFT) == DEVICE_SIGNAL_READABLE, "");
static_assert((POLLPRI << POLL_SHIFT) == DEVICE_SIGNAL_OOB, "");
static_assert((POLLOUT << POLL_SHIFT) == DEVICE_SIGNAL_WRITABLE, "");
static_assert((POLLERR << POLL_SHIFT) == DEVICE_SIGNAL_ERROR, "");
static_assert((POLLHUP << POLL_SHIFT) == DEVICE_SIGNAL_HANGUP, "");

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

mx_status_t mxrio_handle_rpc(mx_handle_t h, mxrio_msg_t* msg, mxrio_cb_t cb, void* cookie) {
    mx_status_t r;

    // NOTE: hcount intentionally received out-of-bound from the message to
    // avoid letting "client-supplied" bytes override the REAL hcount value.
    uint32_t hcount = 0;
    uint32_t dsz = sizeof(mxrio_msg_t);
    if ((r = mx_channel_read(h, 0, msg, msg->handle, dsz, MXIO_MAX_HANDLES, &dsz, &hcount)) < 0) {
        return r;
    }
    // Now, "msg->hcount" can be trusted once again.
    msg->hcount = hcount;

    if (!is_message_reply_valid(msg, dsz)) {
        discard_handles(msg->handle, msg->hcount);
        return MX_ERR_INVALID_ARGS;
    }

    bool is_close = (MXRIO_OP(msg->op) == MXRIO_CLOSE);

    xprintf("handle_rio: op=%s arg=%d len=%u hsz=%d\n",
            mxio_opname(msg->op), msg->arg, msg->datalen, msg->hcount);

    if ((msg->arg = cb(msg, cookie)) == ERR_DISPATCHER_INDIRECT) {
        // callback is handling the reply itself
        // and took ownership of the reply handle
        return MX_OK;
    }
    if ((msg->arg < 0) || !is_message_valid(msg)) {
        // in the event of an error response or bad message
        // release all the handles and data payload
        discard_handles(msg->handle, msg->hcount);
        msg->datalen = 0;
        msg->hcount = 0;
        // specific errors are prioritized over the bad
        // message case which we represent as MX_ERR_INTERNAL
        // to differentiate from MX_ERR_IO on the near side
        // TODO(MG-974): consider a better error code
        msg->arg = (msg->arg < 0) ? msg->arg : MX_ERR_INTERNAL;
    }

    msg->op = MXRIO_STATUS;
    if ((r = mx_channel_write(h, 0, msg, MXRIO_HDR_SZ + msg->datalen, msg->handle, msg->hcount)) < 0) {
        discard_handles(msg->handle, msg->hcount);
    }
    if (is_close) {
        // signals to not perform a close callback
        return ERR_DISPATCHER_DONE;
    } else {
        return r;
    }
}

mx_status_t mxrio_handle_close(mxrio_cb_t cb, void* cookie) {
    mxrio_msg_t msg;

    // remote side was closed;
    msg.op = MXRIO_CLOSE;
    msg.arg = 0;
    msg.datalen = 0;
    msg.hcount = 0;
    cb(&msg, cookie);
    return MX_OK;
}

mx_status_t mxrio_handler(mx_handle_t h, void* _cb, void* cookie) {
    mxrio_cb_t cb = _cb;

    if (h == MX_HANDLE_INVALID) {
        return mxrio_handle_close(cb, cookie);
    } else {
        mxrio_msg_t msg;
        return mxrio_handle_rpc(h, &msg, cb, cookie);
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
        return MX_ERR_INVALID_ARGS;
    }

    msg->txid = atomic_fetch_add(&rio->txid, 1);
    xprintf("txn h=%x txid=%x op=%d len=%u\n", rio->h, msg->txid, msg->op, msg->datalen);

    mx_status_t r;
    mx_status_t rs = MX_ERR_INTERNAL;
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
        if (r == MX_ERR_CALL_FAILED) {
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
        r = MX_ERR_IO;
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

ssize_t mxrio_ioctl(mxio_t* io, uint32_t op, const void* in_buf,
                    size_t in_len, void* out_buf, size_t out_len) {
    mxrio_t* rio = (mxrio_t*)io;
    const uint8_t* data = in_buf;
    mx_status_t r = 0;
    mxrio_msg_t msg;

    if (in_len > MXIO_IOCTL_MAX_INPUT || out_len > MXIO_CHUNK_SIZE) {
        return MX_ERR_INVALID_ARGS;
    }

    memset(&msg, 0, MXRIO_HDR_SZ);
    msg.op = MXRIO_IOCTL;
    msg.datalen = in_len;
    msg.arg = out_len;
    msg.arg2.op = op;

    switch (IOCTL_KIND(op)) {
    case IOCTL_KIND_GET_HANDLE:
        if (out_len < sizeof(mx_handle_t)) {
            return MX_ERR_INVALID_ARGS;
        }
        break;
    case IOCTL_KIND_GET_TWO_HANDLES:
        if (out_len < 2 * sizeof(mx_handle_t)) {
            return MX_ERR_INVALID_ARGS;
        }
        break;
    case IOCTL_KIND_GET_THREE_HANDLES:
        if (out_len < 3 * sizeof(mx_handle_t)) {
            return MX_ERR_INVALID_ARGS;
        }
        break;
    case IOCTL_KIND_SET_HANDLE:
        msg.op = MXRIO_IOCTL_1H;
        if (in_len < sizeof(mx_handle_t)) {
            return MX_ERR_INVALID_ARGS;
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
            r = MX_ERR_IO;
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
            r = MX_ERR_IO;
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

mx_status_t mxrio_close(mxio_t* io) {
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

static mx_status_t mxrio_reply_channel_call(mx_handle_t rio_h, mxrio_msg_t* msg,
                                            mxrio_object_t* info) {
    mx_status_t r;
    mx_handle_t h;
    if ((r = mx_channel_create(0, &h, &msg->handle[0])) < 0) {
        return r;
    }
    msg->hcount = 1;

    // Write the (one-way) request message
    if ((r = mx_channel_write(rio_h, 0, msg, MXRIO_HDR_SZ + msg->datalen,
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
    r = mx_channel_read(h, 0, info, &info->handle[1], dsize,
                        info->hcount, &dsize, &info->hcount);
    if (r < 0) {
        mx_handle_close(h);
        return r;
    }
    info->handle[0] = h;
    info->hcount++;
    if (dsize < MXRIO_OBJECT_MINSIZE) {
        r = MX_ERR_IO;
    } else {
        info->esize = dsize - MXRIO_OBJECT_MINSIZE;
        r = info->status;
    }
    if (r < 0) {
        discard_handles(info->handle, info->hcount);
    }
    return r;
}

// This function always consumes the cnxn handle
// The svc handle is only used to send a message
static mx_status_t mxrio_connect(mx_handle_t svc, mx_handle_t cnxn,
                                 uint32_t op, int32_t flags, uint32_t mode,
                                 const char* name) {
    size_t len = strlen(name);
    if (len >= PATH_MAX) {
        mx_handle_close(cnxn);
        return MX_ERR_BAD_PATH;
    }

    mxrio_msg_t msg;
    memset(&msg, 0, MXRIO_HDR_SZ);
    msg.op = op;
    msg.datalen = len;
    msg.arg = O_PIPELINE | flags;
    msg.arg2.mode = mode;
    msg.hcount = 1;
    msg.handle[0] = cnxn;
    memcpy(msg.data, name, len);

    mx_status_t r;
    if ((r = mx_channel_write(svc, 0, &msg, MXRIO_HDR_SZ + msg.datalen, msg.handle, 1)) < 0) {
        mx_handle_close(cnxn);
        return r;
    }

    return MX_OK;
}

mx_status_t mxio_service_connect(const char* svcpath, mx_handle_t h) {
    if (svcpath == NULL) {
        mx_handle_close(h);
        return MX_ERR_INVALID_ARGS;
    }
    // Otherwise attempt to connect through the root namespace
    if (mxio_root_ns != NULL) {
        return mxio_ns_connect(mxio_root_ns, svcpath, h);
    }
    // Otherwise we fail
    mx_handle_close(h);
    return MX_ERR_NOT_FOUND;
}

mx_status_t mxio_service_connect_at(mx_handle_t dir, const char* path, mx_handle_t h) {
    if (path == NULL) {
        mx_handle_close(h);
        return MX_ERR_INVALID_ARGS;
    }
    if (dir == MX_HANDLE_INVALID) {
        mx_handle_close(h);
        return MX_ERR_UNAVAILABLE;
    }
    return mxrio_connect(dir, h, MXRIO_OPEN, O_RDWR, 0755, path);
}

mx_handle_t mxio_service_clone(mx_handle_t svc) {
    mx_handle_t cli, srv;
    mx_status_t r;
    if (svc == MX_HANDLE_INVALID) {
        return MX_HANDLE_INVALID;
    }
    if ((r = mx_channel_create(0, &cli, &srv)) < 0) {
        return MX_HANDLE_INVALID;
    }
    if ((r = mxrio_connect(svc, srv, MXRIO_CLONE, O_RDWR, 0755, "")) < 0) {
        mx_handle_close(cli);
        return MX_HANDLE_INVALID;
    }
    return cli;
}

mx_status_t mxrio_misc(mxio_t* io, uint32_t op, int64_t off,
                       uint32_t maxreply, void* ptr, size_t len) {
    mxrio_t* rio = (mxrio_t*)io;
    mxrio_msg_t msg;
    mx_status_t r;

    if ((len > MXIO_CHUNK_SIZE) || (maxreply > MXIO_CHUNK_SIZE)) {
        return MX_ERR_INVALID_ARGS;
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

    switch (op) {
    case MXRIO_MMAP: {
        // Ops which receive single handles:
        if ((msg.hcount != 1) || (msg.datalen > maxreply)) {
            discard_handles(msg.handle, msg.hcount);
            return MX_ERR_IO;
        }
        r = msg.handle[0];
        memcpy(ptr, msg.data, msg.datalen);
        break;
    }
    case MXRIO_FCNTL:
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
            return MX_ERR_IO;
        }
        if (ptr && msg.datalen > 0) {
            memcpy(ptr, msg.data, msg.datalen);
        }
    }
    return r;
}

mx_status_t mxio_create_fd(mx_handle_t* handles, uint32_t* types, size_t hcount,
                           int* fd_out) {
    mxio_t* io;
    mx_status_t r;
    int fd;
    uint32_t type;

    switch (PA_HND_TYPE(types[0])) {
    case PA_MXIO_REMOTE:
        type = MXIO_PROTOCOL_REMOTE;
        break;
    case PA_MXIO_PIPE:
        type = MXIO_PROTOCOL_PIPE;
        break;
    case PA_MXIO_SOCKET:
        type = MXIO_PROTOCOL_SOCKET_CONNECTED;
        break;
    default:
        r = MX_ERR_IO;
        goto fail;
    }

    if ((r = mxio_from_handles(type, handles, hcount, NULL, 0, &io)) != MX_OK) {
        goto fail;
    }

    fd = mxio_bind_to_fd(io, -1, 0);
    if (fd < 0) {
        mxio_close(io);
        mxio_release(io);
        return MX_ERR_BAD_STATE;
    }

    *fd_out = fd;
    return MX_OK;
fail:
    for (size_t i = 0; i < hcount; i++) {
        mx_handle_close(handles[i]);
    }
    return r;
}

mx_status_t mxio_from_handles(uint32_t type, mx_handle_t* handles, int hcount,
                              void* extra, uint32_t esize, mxio_t** out) {
    // All failure cases which require discard_handles set r and break
    // to the end. All other cases in which handle ownership is moved
    // on return locally.
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
            r = MX_ERR_INVALID_ARGS;
            break;
        }
        if (io == NULL) {
            return MX_ERR_NO_RESOURCES;
        } else {
            *out = io;
            return MX_OK;
        }
        break;
    case MXIO_PROTOCOL_SERVICE:
        if (hcount != 1) {
            r = MX_ERR_INVALID_ARGS;
            break;
        } else if ((*out = mxio_service_create(handles[0])) == NULL) {
            return MX_ERR_NO_RESOURCES;
        } else {
            return MX_OK;
        }
        break;
    case MXIO_PROTOCOL_PIPE:
        if (hcount != 1) {
            r = MX_ERR_INVALID_ARGS;
            break;
        } else if ((*out = mxio_pipe_create(handles[0])) == NULL) {
            return MX_ERR_NO_RESOURCES;
        } else {
            return MX_OK;
        }
    case MXIO_PROTOCOL_VMOFILE: {
        mx_off_t* args = extra;
        if ((hcount != 2) || (esize != (sizeof(mx_off_t) * 2))) {
            r = MX_ERR_INVALID_ARGS;
            break;
        }
        // Currently, VMO Files don't use a client-side control channel.
        mx_handle_close(handles[0]);
        if ((*out = mxio_vmofile_create(handles[1], args[0], args[1])) == NULL) {
            return MX_ERR_NO_RESOURCES;
        } else {
            return MX_OK;
        }
    }
    case MXIO_PROTOCOL_SOCKET_CONNECTED:
    case MXIO_PROTOCOL_SOCKET: {
        int flags = (type == MXIO_PROTOCOL_SOCKET_CONNECTED) ? MXIO_FLAG_SOCKET_CONNECTED : 0;
        if (hcount == 1) {
            io = mxio_socket_create(handles[0], MX_HANDLE_INVALID, flags);
        } else if (hcount == 2) {
            io = mxio_socket_create(handles[0], handles[1], flags);
        } else {
            r = MX_ERR_INVALID_ARGS;
            break;
        }
        if (io == NULL) {
            return MX_ERR_NO_RESOURCES;
        } else {
            *out = io;
            return MX_OK;
        }
    }
    default:
        r = MX_ERR_NOT_SUPPORTED;
        break;
    }
    discard_handles(handles, hcount);
    return r;
}

mx_status_t mxrio_getobject(mx_handle_t rio_h, uint32_t op, const char* name,
                            int32_t flags, uint32_t mode,
                            mxrio_object_t* info) {
    if (name == NULL) {
        return MX_ERR_INVALID_ARGS;
    }

    size_t len = strlen(name);
    if (len >= PATH_MAX) {
        return MX_ERR_BAD_PATH;
    }

    if (flags & O_PIPELINE) {
        mx_handle_t h0, h1;
        mx_status_t r;
        if ((r = mx_channel_create(0, &h0, &h1)) < 0) {
            return r;
        }
        if ((r = mxrio_connect(rio_h, h1, MXRIO_OPEN, flags, mode, name)) < 0) {
            mx_handle_close(h0);
            return r;
        }
        // fake up a reply message since pipelined opens don't generate one
        info->status = MX_OK;
        info->type = MXIO_PROTOCOL_REMOTE;
        info->esize = 0;
        info->hcount = 1;
        info->handle[0] = h0;
        return MX_OK;
    } else {
        mxrio_msg_t msg;
        memset(&msg, 0, MXRIO_HDR_SZ);
        msg.op = op;
        msg.datalen = len;
        msg.arg = flags;
        msg.arg2.mode = mode;
        memcpy(msg.data, name, len);

        return mxrio_reply_channel_call(rio_h, &msg, info);
    }
}

mx_status_t mxrio_open_handle(mx_handle_t h, const char* path, int32_t flags,
                              uint32_t mode, mxio_t** out) {
    mxrio_object_t info;
    mx_status_t r = mxrio_getobject(h, MXRIO_OPEN, path, flags, mode, &info);
    if (r < 0) {
        return r;
    }
    return mxio_from_handles(info.type, info.handle, info.hcount, info.extra, info.esize, out);
}

mx_status_t mxrio_open_handle_raw(mx_handle_t h, const char* path, int32_t flags,
                                  uint32_t mode, mx_handle_t *out) {
    mxrio_object_t info;
    mx_status_t r = mxrio_getobject(h, MXRIO_OPEN, path, flags, mode, &info);
    if (r < 0) {
        return r;
    }
    if ((info.type == MXIO_PROTOCOL_REMOTE) && (info.hcount > 0)) {
        for (unsigned n = 1; n < info.hcount; n++) {
            mx_handle_close(info.handle[n]);
        }
        *out = info.handle[0];
        return MX_OK;
    }
    for (unsigned n = 0; n < info.hcount; n++) {
        mx_handle_close(info.handle[n]);
    }
    return MX_ERR_WRONG_TYPE;
}

mx_status_t mxrio_open(mxio_t* io, const char* path, int32_t flags, uint32_t mode, mxio_t** out) {
    mxrio_t* rio = (void*)io;
    mxrio_object_t info;
    mx_status_t r = mxrio_getobject(rio->h, MXRIO_OPEN, path, flags, mode, &info);
    if (r < 0) {
        return r;
    }
    return mxio_from_handles(info.type, info.handle, info.hcount, info.extra, info.esize, out);
}

static mx_status_t mxrio_clone(mxio_t* io, mx_handle_t* handles, uint32_t* types) {
    mxrio_t* rio = (void*)io;
    mxrio_object_t info;
    mx_status_t r = mxrio_getobject(rio->h, MXRIO_CLONE, "", 0, 0, &info);
    if (r < 0) {
        return r;
    }
    for (unsigned i = 0; i < info.hcount; i++) {
        types[i] = PA_MXIO_REMOTE;
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
    types[0] = PA_MXIO_REMOTE;
    if (rio->h2 != 0) {
        handles[1] = rio->h2;
        types[1] = PA_MXIO_REMOTE;
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

    mx_signals_t signals = 0;
    // Manually add signals that don't fit within POLL_MASK
    if (events & POLLRDHUP) {
        signals |= MX_CHANNEL_PEER_CLOSED;
    }

    // POLLERR is always detected
    *_signals = (((POLLERR | events) & POLL_MASK) << POLL_SHIFT) | signals;
}

static void mxrio_wait_end(mxio_t* io, mx_signals_t signals, uint32_t* _events) {
    // Manually add events that don't fit within POLL_MASK
    uint32_t events = 0;
    if (signals & MX_CHANNEL_PEER_CLOSED) {
        events |= POLLRDHUP;
    }
    *_events = ((signals >> POLL_SHIFT) & POLL_MASK) | events;
}

static mxio_ops_t mx_remote_ops = {
    .read = mxrio_read,
    .read_at = mxrio_read_at,
    .write = mxrio_write,
    .write_at = mxrio_write_at,
    .recvfrom = mxio_default_recvfrom,
    .sendto = mxio_default_sendto,
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
    .shutdown = mxio_default_shutdown,
    .posix_ioctl = mxio_default_posix_ioctl,
    .get_vmo = mxio_default_get_vmo,
};

mxio_t* mxio_remote_create(mx_handle_t h, mx_handle_t e) {
    mxrio_t* rio = calloc(1, sizeof(*rio));
    if (rio == NULL) {
        mx_handle_close(h);
        mx_handle_close(e);
        return NULL;
    }
    rio->io.ops = &mx_remote_ops;
    rio->io.magic = MXIO_MAGIC;
    atomic_init(&rio->io.refcount, 1);
    rio->h = h;
    rio->h2 = e;
    return &rio->io;
}
