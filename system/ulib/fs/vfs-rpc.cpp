// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>

#include <magenta/process.h>
#include <mxio/debug.h>
#include <mxio/dispatcher.h>
#include <mxio/io.h>
#include <mxio/remoteio.h>
#include <mxio/vfs.h>
#include <mxtl/auto_call.h>
#include <mxtl/auto_lock.h>
#include <mxtl/ref_ptr.h>

#include "vfs-internal.h"

#define MXDEBUG 0

typedef struct vfs_iostate {
    mxtl::RefPtr<Vnode> vn;
    // Handle to event which allows client to refer to open vnodes in multi-patt
    // operations (see: link, rename). Defaults to MX_HANDLE_INVALID.
    // Validated on the server side using cookies.
    mx_handle_t token;
    vdircookie_t dircookie;
    size_t io_off;
    uint32_t io_flags;
} vfs_iostate_t;

namespace fs {
namespace {

static void txn_handoff_open(mx_handle_t srv, mx_handle_t rh,
                             const char* path, uint32_t flags, uint32_t mode) {
    mxrio_msg_t msg;
    memset(&msg, 0, MXRIO_HDR_SZ);
    size_t len = strlen(path);
    msg.op = MXRIO_OPEN;
    msg.arg = flags;
    msg.arg2.mode = mode;
    msg.datalen = static_cast<uint32_t>(len) + 1;
    memcpy(msg.data, path, len + 1);
    mxrio_txn_handoff(srv, rh, &msg);
}

// Initializes io state for a vnode and attaches it to a dispatcher.
void vfs_rpc_open(mxrio_msg_t* msg, mx_handle_t rh, mxtl::RefPtr<Vnode> vn, const char* path, uint32_t flags,
                  uint32_t mode) {
    mx_status_t r;

    // The pipeline directive instructs the VFS layer to open the vnode
    // immediately, rather than describing the VFS object to the caller.
    // We check it early so we can throw away the protocol part of flags.
    bool pipeline = flags & MXRIO_OFLAG_PIPELINE;
    uint32_t open_flags = flags & (~MXRIO_OFLAG_MASK);

    {
        mxtl::AutoLock lock(&vfs_lock);
        r = Vfs::Open(mxtl::move(vn), &vn, path, &path, open_flags, mode);
    }

    mxrio_object_t obj;
    memset(&obj, 0, sizeof(obj));
    if (r < 0) {
        xprintf("vfs: open: r=%d\n", r);
        goto done;
    } else if (r > 0) {
        // Remote handoff, either to a remote device or a remote filesystem node.
        txn_handoff_open(r, rh, path, flags, mode);
        return;
    }

    // Acquire the handles to the VFS object
    if ((r = vn->GetHandles(flags, obj.handle, &obj.type, obj.extra, &obj.esize)) < 0) {
        vn->Close();
        goto done;
    }

done:
    // If r >= 0, then we hold a reference to vn from open.
    // Otherwise, vn is closed, and we're simply responding to the client.

    if (pipeline && r > 0) {
        // If a pipeline open was requested, but extra handles are required, then
        // we cannot complete the open in a pipelined fashion.
        while (r-- > 0) {
            mx_handle_close(obj.handle[r]);
        }
        vn->Close();
        mx_handle_close(rh);
        return;
    }

    if (!pipeline) {
        // Describe the VFS object to the caller in the non-pipelined case.
        obj.status = (r < 0) ? r : NO_ERROR;
        obj.hcount = (r > 0) ? r : 0;
        mx_channel_write(rh, 0, &obj, static_cast<uint32_t>(MXRIO_OBJECT_MINSIZE + obj.esize),
                         obj.handle, obj.hcount);
        if (r < 0) {
            mx_handle_close(rh);
            return;
        }
    }

    vn->Serve(rh, open_flags);
}

// Consumes rh.
void mxrio_reply_channel_status(mx_handle_t rh, mx_status_t status) {
    struct {
        mx_status_t status;
        uint32_t type;
    } reply = {status, 0};
    mx_channel_write(rh, 0, &reply, MXRIO_OBJECT_MINSIZE, nullptr, 0);
    mx_handle_close(rh);
}

} // namespace anonymous

mx_status_t Vnode::Serve(mx_handle_t h, uint32_t flags) {
    mx_status_t r;
    vfs_iostate_t* ios;

    if ((ios = static_cast<vfs_iostate_t*>(calloc(1, sizeof(vfs_iostate_t)))) == nullptr) {
        mx_handle_close(h);
        return ERR_NO_MEMORY;
    }
    ios->vn = mxtl::RefPtr<Vnode>(this);
    ios->io_flags = flags;

    if ((r = GetDispatcher()->AddVFSHandler(h, reinterpret_cast<void*>(vfs_handler), ios)) < 0) {
        mx_handle_close(h);
        free(ios);
        return r;
    }
    return NO_ERROR;
}

} // namespace fs

#define TOKEN_RIGHTS (MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER)

static mx_status_t iostate_get_token(uint64_t vnode_cookie, vfs_iostate* ios, mx_handle_t* out) {
    mxtl::AutoLock lock(&vfs_lock);
    mx_status_t r;

    if (ios->token != MX_HANDLE_INVALID) {
        // Token has already been set for this iostate
        if ((r = mx_handle_duplicate(ios->token, TOKEN_RIGHTS, out) != NO_ERROR)) {
            return r;
        }
        return NO_ERROR;
    } else if ((r = mx_event_create(0, &ios->token)) != NO_ERROR) {
        return r;
    } else if ((r = mx_handle_duplicate(ios->token, TOKEN_RIGHTS, out) != NO_ERROR)) {
        mx_handle_close(ios->token);
        ios->token = MX_HANDLE_INVALID;
        return r;
    } else if ((r = mx_object_set_cookie(ios->token, mx_process_self(), vnode_cookie)) != NO_ERROR) {
        mx_handle_close(*out);
        mx_handle_close(ios->token);
        ios->token = MX_HANDLE_INVALID;
        return r;
    }
    return sizeof(mx_handle_t);
}

mx_status_t vfs_handler_vn(mxrio_msg_t* msg, mxtl::RefPtr<Vnode> vn, vfs_iostate* ios) {
    uint32_t len = msg->datalen;
    int32_t arg = msg->arg;
    msg->datalen = 0;

    // ensure handle count specified by opcode matches reality
    if (msg->hcount != MXRIO_HC(msg->op)) {
        for (unsigned i = 0; i < msg->hcount; i++) {
            mx_handle_close(msg->handle[i]);
        }
        return ERR_IO;
    }
    msg->hcount = 0;

    switch (MXRIO_OP(msg->op)) {
    case MXRIO_OPEN: {
        char* path = (char*)msg->data;
        if ((len < 1) || (len > PATH_MAX)) {
            fs::mxrio_reply_channel_status(msg->handle[0], ERR_INVALID_ARGS);
        } else {
            path[len] = 0;
            xprintf("vfs: open name='%s' flags=%d mode=%u\n", path, arg, msg->arg2.mode);
            fs::vfs_rpc_open(msg, msg->handle[0], vn, path, arg, msg->arg2.mode);
        }
        return ERR_DISPATCHER_INDIRECT;
    }
    case MXRIO_CLOSE: {
        {
            mxtl::AutoLock lock(&vfs_lock);
            if (ios->token != MX_HANDLE_INVALID) {
                // The token is nullified here to prevent the following race condition:
                // 1) Open
                // 2) GetToken
                // 3) Close + Release Vnode
                // 4) Use token handle to access defunct vnode (or a different vnode,
                //    if the memory for it is reallocated).
                //
                // By nullifying the token cookie, any remaining handles to the event will
                // be ignored by the filesystem server.
                mx_object_set_cookie(ios->token, mx_process_self(), 0);
                mx_handle_close(ios->token);
                ios->token = MX_HANDLE_INVALID;
            }
        }

        // this will drop the ref on the vn
        mx_status_t status = vn->Close();
        ios->vn = nullptr;
        free(ios);
        return status;
    }
    case MXRIO_CLONE: {
        if (!(arg & MXRIO_OFLAG_PIPELINE)) {
            mxrio_object_t obj;
            memset(&obj, 0, MXRIO_OBJECT_MINSIZE);
            obj.type = MXIO_PROTOCOL_REMOTE;
            mx_channel_write(msg->handle[0], 0, &obj, MXRIO_OBJECT_MINSIZE, 0, 0);
        }
        vn->Serve(msg->handle[0], ios->io_flags);
        return ERR_DISPATCHER_INDIRECT;
    }
    case MXRIO_READ: {
        ssize_t r = vn->Read(msg->data, arg, ios->io_off);
        if (r >= 0) {
            ios->io_off += r;
            msg->arg2.off = ios->io_off;
            msg->datalen = static_cast<uint32_t>(r);
        }
        return static_cast<mx_status_t>(r);
    }
    case MXRIO_READ_AT: {
        ssize_t r = vn->Read(msg->data, arg, msg->arg2.off);
        if (r >= 0) {
            msg->datalen = static_cast<uint32_t>(r);
        }
        return static_cast<mx_status_t>(r);
    }
    case MXRIO_WRITE: {
        if (ios->io_flags & O_APPEND) {
            vnattr_t attr;
            mx_status_t r;
            if ((r = vn->Getattr(&attr)) < 0) {
                return r;
            }
            ios->io_off = attr.size;
        }
        ssize_t r = vn->Write(msg->data, len, ios->io_off);
        if (r >= 0) {
            ios->io_off += r;
            msg->arg2.off = ios->io_off;
        }
        return static_cast<mx_status_t>(r);
    }
    case MXRIO_WRITE_AT: {
        ssize_t r = vn->Write(msg->data, len, msg->arg2.off);
        return static_cast<mx_status_t>(r);
    }
    case MXRIO_SEEK: {
        vnattr_t attr;
        mx_status_t r;
        if ((r = vn->Getattr(&attr)) < 0) {
            return r;
        }
        size_t n;
        switch (arg) {
        case SEEK_SET:
            if (msg->arg2.off < 0) {
                return ERR_INVALID_ARGS;
            }
            n = msg->arg2.off;
            break;
        case SEEK_CUR:
            n = ios->io_off + msg->arg2.off;
            if (msg->arg2.off < 0) {
                // if negative seek
                if (n > ios->io_off) {
                    // wrapped around. attempt to seek before start
                    return ERR_INVALID_ARGS;
                }
            } else {
                // positive seek
                if (n < ios->io_off) {
                    // wrapped around. overflow
                    return ERR_INVALID_ARGS;
                }
            }
            break;
        case SEEK_END:
            n = attr.size + msg->arg2.off;
            if (msg->arg2.off < 0) {
                // if negative seek
                if (n > attr.size) {
                    // wrapped around. attempt to seek before start
                    return ERR_INVALID_ARGS;
                }
            } else {
                // positive seek
                if (n < attr.size) {
                    // wrapped around
                    return ERR_INVALID_ARGS;
                }
            }
            break;
        default:
            return ERR_INVALID_ARGS;
        }
        if (vn->IsDevice()) {
            if (n > attr.size) {
                // devices may not seek past the end
                return ERR_INVALID_ARGS;
            }
        }
        ios->io_off = n;
        msg->arg2.off = ios->io_off;
        return NO_ERROR;
    }
    case MXRIO_STAT: {
        mx_status_t r;
        msg->datalen = sizeof(vnattr_t);
        if ((r = vn->Getattr((vnattr_t*)msg->data)) < 0) {
            return r;
        }
        return msg->datalen;
    }
    case MXRIO_SETATTR: {
        mx_status_t r = vn->Setattr((vnattr_t*)msg->data);
        return r;
    }
    case MXRIO_READDIR: {
        if (arg > MXIO_CHUNK_SIZE) {
            return ERR_INVALID_ARGS;
        }
        if (msg->arg2.off == READDIR_CMD_RESET) {
            memset(&ios->dircookie, 0, sizeof(ios->dircookie));
        }
        mx_status_t r;
        {
            mxtl::AutoLock lock(&vfs_lock);
            r = vn->Readdir(&ios->dircookie, msg->data, arg);
        }
        if (r >= 0) {
            msg->datalen = r;
        }
        return r;
    }
    case MXRIO_IOCTL_1H: {
        if ((len > MXIO_IOCTL_MAX_INPUT) ||
            (arg > (ssize_t)sizeof(msg->data)) ||
            (IOCTL_KIND(msg->arg2.op) != IOCTL_KIND_SET_HANDLE)) {
            mx_handle_close(msg->handle[0]);
            return ERR_INVALID_ARGS;
        }
        if (len < sizeof(mx_handle_t)) {
            len = sizeof(mx_handle_t);
        }

        char in_buf[MXIO_IOCTL_MAX_INPUT];
        // The sending side copied the handle into msg->handle[0]
        // so that it would be sent via channel_write().  Here we
        // copy the local version back into the space in the buffer
        // that the original occupied.
        memcpy(in_buf, msg->handle, sizeof(mx_handle_t));
        memcpy(in_buf + sizeof(mx_handle_t), msg->data + sizeof(mx_handle_t),
               len - sizeof(mx_handle_t));

        ssize_t r = fs::Vfs::Ioctl(mxtl::move(vn), msg->arg2.op, in_buf, len, msg->data, arg);

        if (r == ERR_NOT_SUPPORTED) {
            mx_handle_close(msg->handle[0]);
        }

        return static_cast<mx_status_t>(r);
    }
    case MXRIO_IOCTL: {
        if (len > MXIO_IOCTL_MAX_INPUT ||
            (arg > (ssize_t)sizeof(msg->data)) ||
            (IOCTL_KIND(msg->arg2.op) == IOCTL_KIND_SET_HANDLE)) {
            return ERR_INVALID_ARGS;
        }
        char in_buf[MXIO_IOCTL_MAX_INPUT];
        memcpy(in_buf, msg->data, len);

        ssize_t r;
        switch (msg->arg2.op) {
        // Ioctls which act on iostate
        case IOCTL_VFS_GET_TOKEN: {
            if (arg != sizeof(mx_handle_t)) {
                r = ERR_INVALID_ARGS;
            } else {
                mx_handle_t* out = reinterpret_cast<mx_handle_t*>(msg->data);
                r = iostate_get_token(reinterpret_cast<uint64_t>(vn.get()), ios, out);
            }
            break;
        }
        default:
            r = fs::Vfs::Ioctl(mxtl::move(vn), msg->arg2.op, in_buf, len, msg->data, arg);
        }
        if (r >= 0) {
            switch (IOCTL_KIND(msg->arg2.op)) {
            case IOCTL_KIND_DEFAULT:
                break;
            case IOCTL_KIND_GET_HANDLE:
                msg->hcount = 1;
                memcpy(msg->handle, msg->data, sizeof(mx_handle_t));
                break;
            case IOCTL_KIND_GET_TWO_HANDLES:
                msg->hcount = 2;
                memcpy(msg->handle, msg->data, 2 * sizeof(mx_handle_t));
                break;
            case IOCTL_KIND_GET_THREE_HANDLES:
                msg->hcount = 3;
                memcpy(msg->handle, msg->data, 3 * sizeof(mx_handle_t));
                break;
            }
            msg->arg2.off = 0;
            msg->datalen = static_cast<uint32_t>(r);
        }
        return static_cast<uint32_t>(r);
    }
    case MXRIO_TRUNCATE: {
        if (msg->arg2.off < 0) {
            return ERR_INVALID_ARGS;
        }
        return vn->Truncate(msg->arg2.off);
    }
    case MXRIO_RENAME:
    case MXRIO_LINK: {
        // Regardless of success or failure, we'll close the client-provided
        // vnode token handle.
        auto ac = mxtl::MakeAutoCall([&msg](){ mx_handle_close(msg->handle[0]); });

        if (len < 4) { // At least one byte for src + dst + null terminators
            return ERR_INVALID_ARGS;
        }

        char* data_end = (char*)(msg->data + len - 1);
        *data_end = '\0';
        const char* oldname = (const char*)msg->data;
        size_t oldlen = strlen(oldname);
        const char* newname = (const char*)msg->data + (oldlen + 1);

        if (data_end <= newname) {
            return ERR_INVALID_ARGS;
        }

        mx_status_t r;
        uint64_t vcookie;

        mxtl::AutoLock lock(&vfs_lock);
        if ((r = mx_object_get_cookie(msg->handle[0], mx_process_self(), &vcookie)) < 0) {
            // TODO(smklein): Return a more specific error code for "token not from this server"
            return ERR_INVALID_ARGS;
        }

        if (vcookie == 0) {
            // Client closed the channel associated with the token
            return ERR_INVALID_ARGS;
        }

        mxtl::RefPtr<Vnode> target_parent =
                mxtl::RefPtr<Vnode>(reinterpret_cast<Vnode*>(vcookie));
        switch (MXRIO_OP(msg->op)) {
        case MXRIO_RENAME:
            return fs::Vfs::Rename(mxtl::move(vn), mxtl::move(target_parent), oldname, newname);
        case MXRIO_LINK:
            return fs::Vfs::Link(mxtl::move(vn), mxtl::move(target_parent), oldname, newname);
        }
        assert(false);
    }
    case MXRIO_MMAP: {
        if (len != sizeof(mxrio_mmap_data_t)) {
            return ERR_INVALID_ARGS;
        }
        mxrio_mmap_data_t* data = reinterpret_cast<mxrio_mmap_data_t*>(msg->data);

        mx_status_t status = vn->Mmap(data->flags, data->length, &data->offset,
                                      &msg->handle[0]);
        if (status == NO_ERROR) {
            msg->hcount = 1;
        }
        return status;
    }
    case MXRIO_SYNC: {
        return vn->Sync();
    }
    case MXRIO_UNLINK:
        return fs::Vfs::Unlink(mxtl::move(vn), (const char*)msg->data, len);
    default:
        // close inbound handles so they do not leak
        for (unsigned i = 0; i < MXRIO_HC(msg->op); i++) {
            mx_handle_close(msg->handle[i]);
        }
        return ERR_NOT_SUPPORTED;
    }
}

// TODO(orr): temporary; prevent multithread weirdness while we
// make locking more fine grained
static mtx_t vfs_big_lock = MTX_INIT;

mx_status_t vfs_handler(mxrio_msg_t* msg, void* cookie) {
    vfs_iostate_t* ios = static_cast<vfs_iostate_t*>(cookie);

    mxtl::AutoLock lock(&vfs_big_lock);
    mxtl::RefPtr<Vnode> vn = ios->vn;
    mx_status_t status = vfs_handler_vn(msg, mxtl::move(vn), ios);
    return status;
}

mx_handle_t vfs_rpc_server(mx_handle_t h, mxtl::RefPtr<Vnode> vn) {
    vfs_iostate_t* ios;
    mx_status_t r;

    if ((ios = (vfs_iostate_t*)calloc(1, sizeof(vfs_iostate_t))) == NULL)
        return ERR_NO_MEMORY;
    ios->vn = mxtl::move(vn);  // reference passed in by caller
    ios->io_flags = 0;

    mxio_dispatcher_t* vfs_dispatcher;
    if ((r = mxio_dispatcher_create(&vfs_dispatcher, mxrio_handler)) < 0) {
        free(ios);
        return r;
    }

    // Tell the calling process that we've mounted
    if ((r = mx_object_signal_peer(h, 0, MX_USER_SIGNAL_0)) != NO_ERROR) {
        free(ios);
        return r;
    }

    if ((r = mxio_dispatcher_add(vfs_dispatcher, h, (void*) vfs_handler, ios)) < 0) {
        free(ios);
        return r;
    }

    // calling thread blocks
    mxio_dispatcher_run(vfs_dispatcher);
    return NO_ERROR;
}
