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

#include "vfs-internal.h"

#define MXDEBUG 0

namespace fs {
namespace {

void txn_handoff_open(mx_handle_t srv, mx_handle_t rh,
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

void txn_handoff_clone(mx_handle_t srv, mx_handle_t rh) {
    mxrio_msg_t msg;
    memset(&msg, 0, MXRIO_HDR_SZ);
    msg.op = MXRIO_CLONE;
    mxrio_txn_handoff(srv, rh, &msg);
}

// Initializes io state for a vnode and attaches it to a dispatcher.
void vfs_rpc_open(mxrio_msg_t* msg, mx_handle_t rh, fs::Vnode* vn, const char* path, uint32_t flags,
                  uint32_t mode) {
    mxrio_object_t obj;
    memset(&obj, 0, sizeof(obj));

    mx_status_t r;

    {
        mxtl::AutoLock lock(&vfs_lock);
        r = Vfs::Open(vn, &vn, path, &path, flags, mode);
    }

    if (r < 0) {
        xprintf("vfs: open: r=%d\n", r);
        goto done;
    }
    if (r > 0) {
        //TODO: unify remote vnodes and remote devices
        //      eliminate vfs_get_handles() and the other
        //      reply pipe path
        txn_handoff_open(r, rh, path, flags, mode);
        return;
    }

    obj.esize = 0;
    if ((r = vn->GetHandles(flags, obj.handle, &obj.type, obj.extra, &obj.esize)) < 0) {
        vn->Close();
        goto done;
    }
    if (obj.type == 0) {
        // device is non-local, handle is the server that
        // can clone it for us, redirect the rpc to there
        txn_handoff_open(obj.handle[0], rh, ".", flags, mode);
        vn->RefRelease();
        return;
    }

    // drop the ref from VfsOpen
    // the backend behind get_handles holds the on-going ref
    vn->RefRelease();
    obj.hcount = r;
    r = NO_ERROR;

done:
    obj.status = r;
    xprintf("vfs: open: r=%d h=%x\n", r, obj.handle[0]);
    mx_channel_write(rh, 0, &obj, static_cast<uint32_t>(MXRIO_OBJECT_MINSIZE + obj.esize),
                     obj.handle, obj.hcount);
    mx_handle_close(rh);
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

mx_status_t Vnode::Serve(uint32_t flags, mx_handle_t* out, uint32_t* type) {
    mx_handle_t h[2];
    mx_status_t r;
    vfs_iostate_t* ios;

    if ((ios = static_cast<vfs_iostate_t*>(calloc(1, sizeof(vfs_iostate_t)))) == nullptr) {
        return ERR_NO_MEMORY;
    }
    ios->vn = this;
    ios->io_flags = flags;

    if ((r = mx_channel_create(0, &h[0], &h[1])) < 0) {
        free(ios);
        return r;
    }
    if ((r = mxio_dispatcher_add(vfs_dispatcher, h[0], (void*) vfs_handler, ios)) < 0) {
        mx_handle_close(h[0]);
        mx_handle_close(h[1]);
        free(ios);
        return r;
    }
    // take a ref for the dispatcher
    RefAcquire();
    out[0] = h[1];
    type[0] = MXIO_PROTOCOL_REMOTE;
    return 1;
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

mx_status_t vfs_handler_generic(mxrio_msg_t* msg, mx_handle_t rh, void* cookie) {
    vfs_iostate_t* ios = static_cast<vfs_iostate_t*>(cookie);
    Vnode* vn = ios->vn;
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
    case MXRIO_CLOSE:
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
        fs::Vfs::Close(vn);
        free(ios);
        return NO_ERROR;
    case MXRIO_CLONE: {
        mxrio_object_t obj;
        obj.status = vn->Serve(ios->io_flags, obj.handle, &obj.type);
        mx_channel_write(msg->handle[0], 0, &obj, MXRIO_OBJECT_MINSIZE,
                         obj.handle, (obj.status < 0) ? 0 : 1);
        mx_handle_close(msg->handle[0]);
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

        ssize_t r = fs::Vfs::Ioctl(vn, msg->arg2.op, in_buf, len, msg->data, arg);

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
        case IOCTL_DEVMGR_GET_TOKEN: {
            if (arg != sizeof(mx_handle_t)) {
                r = ERR_INVALID_ARGS;
            } else {
                mx_handle_t* out = reinterpret_cast<mx_handle_t*>(msg->data);
                r = iostate_get_token(reinterpret_cast<uint64_t>(vn), ios, out);
            }
            break;
        }
        default:
            r = fs::Vfs::Ioctl(vn, msg->arg2.op, in_buf, len, msg->data, arg);
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

        Vnode* target_parent = reinterpret_cast<Vnode*>(vcookie);
        switch (MXRIO_OP(msg->op)) {
        case MXRIO_RENAME:
            return fs::Vfs::Rename(vn, target_parent, oldname, newname);
        case MXRIO_LINK:
            return fs::Vfs::Link(vn, target_parent, oldname, newname);
        }
        assert(false);
    }
    case MXRIO_SYNC: {
        return vn->Sync();
    }
    case MXRIO_UNLINK:
        return fs::Vfs::Unlink(vn, (const char*)msg->data, len);
    default:
        // close inbound handles so they do not leak
        for (unsigned i = 0; i < MXRIO_HC(msg->op); i++) {
            mx_handle_close(msg->handle[i]);
        }
        return ERR_NOT_SUPPORTED;
    }
}
