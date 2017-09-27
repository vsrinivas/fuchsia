// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/connection.h>

#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <fdio/debug.h>
#include <fdio/io.h>
#include <fdio/remoteio.h>
#include <fdio/vfs.h>
#include <fs/vnode.h>
#include <zircon/assert.h>

#define MXDEBUG 0

namespace fs {
namespace {

void WriteErrorReply(zx::channel channel, zx_status_t status) {
    struct {
        zx_status_t status;
        uint32_t type;
    } reply = {status, 0};
    channel.write(0, &reply, ZXRIO_OBJECT_MINSIZE, nullptr, 0);
}

zx_status_t HandoffOpenTransaction(zx_handle_t srv, zx::channel channel,
                                   fbl::StringPiece path, uint32_t flags, uint32_t mode) {
    zxrio_msg_t msg;
    memset(&msg, 0, ZXRIO_HDR_SZ);
    msg.op = ZXRIO_OPEN;
    msg.arg = flags;
    msg.arg2.mode = mode;
    msg.datalen = static_cast<uint32_t>(path.length());
    memcpy(msg.data, path.begin(), path.length());
    return zxrio_txn_handoff(srv, channel.release(), &msg);
}

// Performs a path walk and opens a connection to another node.
void OpenAt(Vfs* vfs, fbl::RefPtr<Vnode> parent,
            zxrio_msg_t* msg, zx::channel channel,
            fbl::StringPiece path, uint32_t flags, uint32_t mode) {
    // The pipeline directive instructs the VFS layer to open the vnode
    // immediately, rather than describing the VFS object to the caller.
    // We check it early so we can throw away the protocol part of flags.
    bool pipeline = flags & O_PIPELINE;
    uint32_t open_flags = flags & (~O_PIPELINE);
    size_t hcount = 0;

    fbl::RefPtr<Vnode> vnode;
    zx_status_t r = vfs->Open(fbl::move(parent), &vnode, path, &path, open_flags, mode);

    zxrio_object_t obj;
    memset(&obj, 0, sizeof(obj));
    if (r != ZX_OK) {
        xprintf("vfs: open: r=%d\n", r);
    } else if (!(open_flags & O_NOREMOTE) && vnode->IsRemote()) {
        // Remote handoff to a remote filesystem node.
        //
        // TODO(smklein): There exists a race between multiple threads
        // opening a "dead" connection, where the second thread may
        // try to send a txn_handoff_open to a closed handle.
        // See ZX-1161 for more details.
        r = HandoffOpenTransaction(vnode->GetRemote(), fbl::move(channel), path, flags, mode);
        if (r == ZX_ERR_PEER_CLOSED) {
            printf("VFS: Remote filesystem channel closed, unmounting\n");
            zx::channel c;
            vfs->UninstallRemote(vnode, &c);
        }
        return;
    } else {
        // Acquire the handles to the VFS object
        r = vnode->GetHandles(flags, obj.handle, &hcount, &obj.type, obj.extra, &obj.esize);
        if (r != ZX_OK) {
            vnode->Close();
        }
    }

    // If r == ZX_OK, then we hold a reference to vn from open.
    // Otherwise, vn is closed, and we're simply responding to the client.

    if (pipeline && hcount > 0) {
        // If a pipeline open was requested, but extra handles are required, then
        // we cannot complete the open in a pipelined fashion.
        while (hcount-- > 0) {
            zx_handle_close(obj.handle[hcount]);
        }
        vnode->Close();
        return;
    }

    if (!pipeline) {
        // Describe the VFS object to the caller in the non-pipelined case.
        obj.status = r;
        obj.hcount = static_cast<uint32_t>(hcount);
        channel.write(0, &obj, static_cast<uint32_t>(ZXRIO_OBJECT_MINSIZE + obj.esize),
                      obj.handle, obj.hcount);
    }

    if (r != ZX_OK) {
        return;
    }

    // We don't care about the result because we are handing off the channel.
    vnode->Serve(vfs, fbl::move(channel), open_flags);
}

} // namespace

Connection::Connection(Vfs* vfs, fbl::RefPtr<Vnode> vnode,
                       zx::channel channel, uint32_t flags)
    : vfs_(vfs), vnode_(fbl::move(vnode)), channel_(fbl::move(channel)),
      wait_(ZX_HANDLE_INVALID, ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
            ASYNC_FLAG_HANDLE_SHUTDOWN),
      flags_(flags) {
    ZX_DEBUG_ASSERT(vfs);
    ZX_DEBUG_ASSERT(vnode_);
    ZX_DEBUG_ASSERT(channel_);

    wait_.set_handler([this](async_t* async, zx_status_t status,
                             const zx_packet_signal_t* signal) {
        ZX_DEBUG_ASSERT(is_waiting());

        // Handle the message.
        if (status == ZX_OK && (signal->observed & ZX_CHANNEL_READABLE)) {
            status = CallHandler();
            if (status == ZX_OK) {
                return ASYNC_WAIT_AGAIN;
            }
        }
        wait_.set_object(ZX_HANDLE_INVALID);

        // Give the dispatcher a chance to clean up.
        if (status != ERR_DISPATCHER_DONE) {
            CallHandler();
        }

        // Tell the VFS that the connection closed remotely.
        // This might have the side-effect of destroying this object.
        vfs_->OnConnectionClosedRemotely(this);
        return ASYNC_WAIT_FINISHED;
    });
}

Connection::~Connection() {
    // Stop waiting and clean up if still connected.
    if (is_waiting()) {
        zx_status_t status = wait_.Cancel(vfs_->async());
        ZX_DEBUG_ASSERT_MSG(status == ZX_OK, "Could not cancel wait: status=%d", status);
        wait_.set_object(ZX_HANDLE_INVALID);

        CallHandler();
    }

    // Release the token associated with this connection's vnode since the connection
    // will be releasing the vnode's reference once this function returns.
    if (token_) {
        vfs_->TokenDiscard(fbl::move(token_));
    }
}

zx_status_t Connection::Serve() {
    ZX_DEBUG_ASSERT(!is_waiting());

    wait_.set_object(channel_.get());
    zx_status_t status = wait_.Begin(vfs_->async());
    if (status != ZX_OK) {
        wait_.set_object(ZX_HANDLE_INVALID);
    }
    return status;
}

zx_status_t Connection::CallHandler() {
    return zxrio_handler(channel_.get(), (void*)&Connection::HandleMessageThunk, this);
}

zx_status_t Connection::HandleMessageThunk(zxrio_msg_t* msg, void* cookie) {
    Connection* connection = static_cast<Connection*>(cookie);
    return connection->HandleMessage(msg);
}

zx_status_t Connection::HandleMessage(zxrio_msg_t* msg) {
    uint32_t len = msg->datalen;
    int32_t arg = msg->arg;
    msg->datalen = 0;

    // ensure handle count specified by opcode matches reality
    if (msg->hcount != ZXRIO_HC(msg->op)) {
        for (unsigned i = 0; i < msg->hcount; i++) {
            zx_handle_close(msg->handle[i]);
        }
        return ZX_ERR_IO;
    }
    msg->hcount = 0;

    switch (ZXRIO_OP(msg->op)) {
    case ZXRIO_OPEN: {
        char* path = (char*)msg->data;
        zx::channel channel(msg->handle[0]); // take ownership
        if ((len < 1) || (len > PATH_MAX)) {
            WriteErrorReply(fbl::move(channel), ZX_ERR_INVALID_ARGS);
        } else if ((arg & O_ADMIN) && !(flags_ & O_ADMIN)) {
            WriteErrorReply(fbl::move(channel), ZX_ERR_ACCESS_DENIED);
        } else {
            path[len] = 0;
            xprintf("vfs: open name='%s' flags=%d mode=%u\n", path, arg, msg->arg2.mode);
            OpenAt(vfs_, vnode_, msg, fbl::move(channel),
                   fbl::StringPiece(path, len), arg, msg->arg2.mode);
        }
        return ERR_DISPATCHER_INDIRECT;
    }
    case ZXRIO_CLOSE: {
        return vnode_->Close();
    }
    case ZXRIO_CLONE: {
        zx::channel channel(msg->handle[0]); // take ownership
        if (!(arg & O_PIPELINE)) {
            zxrio_object_t obj;
            memset(&obj, 0, ZXRIO_OBJECT_MINSIZE);
            obj.type = FDIO_PROTOCOL_REMOTE;
            channel.write(0, &obj, ZXRIO_OBJECT_MINSIZE, 0, 0);
        }
        vnode_->Serve(vfs_, fbl::move(channel), flags_);
        return ERR_DISPATCHER_INDIRECT;
    }
    case ZXRIO_READ: {
        if (!IsReadable(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }
        size_t actual;
        zx_status_t status = vnode_->Read(msg->data, arg, offset_, &actual);
        if (status == ZX_OK) {
            ZX_DEBUG_ASSERT(actual <= static_cast<size_t>(arg));
            offset_ += actual;
            msg->arg2.off = offset_;
            msg->datalen = static_cast<uint32_t>(actual);
        }
        return status == ZX_OK ? static_cast<zx_status_t>(actual) : status;
    }
    case ZXRIO_READ_AT: {
        if (!IsReadable(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }
        size_t actual;
        zx_status_t status = vnode_->Read(msg->data, arg, msg->arg2.off, &actual);
        if (status == ZX_OK) {
            ZX_DEBUG_ASSERT(actual <= static_cast<size_t>(arg));
            msg->datalen = static_cast<uint32_t>(actual);
        }
        return status == ZX_OK ? static_cast<zx_status_t>(actual) : status;
    }
    case ZXRIO_WRITE: {
        if (!IsWritable(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }

        size_t actual;
        zx_status_t status;
        if (flags_ & O_APPEND) {
            size_t end;
            status = vnode_->Append(msg->data, len, &end, &actual);
            if (status == ZX_OK) {
                offset_ = end;
                msg->arg2.off = offset_;
            }
        } else {
            status = vnode_->Write(msg->data, len, offset_, &actual);
            if (status == ZX_OK) {
                offset_ += actual;
                msg->arg2.off = offset_;
            }
        }
        ZX_DEBUG_ASSERT(actual <= static_cast<size_t>(len));
        return status == ZX_OK ? static_cast<zx_status_t>(actual) : status;
    }
    case ZXRIO_WRITE_AT: {
        if (!IsWritable(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }
        size_t actual;
        zx_status_t status = vnode_->Write(msg->data, len, msg->arg2.off, &actual);
        if (status == ZX_OK) {
            ZX_DEBUG_ASSERT(actual <= static_cast<size_t>(len));
            return static_cast<zx_status_t>(actual);
        }
        return status;
    }
    case ZXRIO_SEEK: {
        vnattr_t attr;
        zx_status_t r;
        if ((r = vnode_->Getattr(&attr)) < 0) {
            return r;
        }
        size_t n;
        switch (arg) {
        case SEEK_SET:
            if (msg->arg2.off < 0) {
                return ZX_ERR_INVALID_ARGS;
            }
            n = msg->arg2.off;
            break;
        case SEEK_CUR:
            n = offset_ + msg->arg2.off;
            if (msg->arg2.off < 0) {
                // if negative seek
                if (n > offset_) {
                    // wrapped around. attempt to seek before start
                    return ZX_ERR_INVALID_ARGS;
                }
            } else {
                // positive seek
                if (n < offset_) {
                    // wrapped around. overflow
                    return ZX_ERR_INVALID_ARGS;
                }
            }
            break;
        case SEEK_END:
            n = attr.size + msg->arg2.off;
            if (msg->arg2.off < 0) {
                // if negative seek
                if (n > attr.size) {
                    // wrapped around. attempt to seek before start
                    return ZX_ERR_INVALID_ARGS;
                }
            } else {
                // positive seek
                if (n < attr.size) {
                    // wrapped around
                    return ZX_ERR_INVALID_ARGS;
                }
            }
            break;
        default:
            return ZX_ERR_INVALID_ARGS;
        }
        offset_ = n;
        msg->arg2.off = offset_;
        return ZX_OK;
    }
    case ZXRIO_STAT: {
        zx_status_t r;
        msg->datalen = sizeof(vnattr_t);
        if ((r = vnode_->Getattr((vnattr_t*)msg->data)) < 0) {
            return r;
        }
        return msg->datalen;
    }
    case ZXRIO_SETATTR: {
        zx_status_t r = vnode_->Setattr((vnattr_t*)msg->data);
        return r;
    }
    case ZXRIO_FCNTL: {
        uint32_t cmd = msg->arg;
        constexpr uint32_t kStatusFlags = O_APPEND;
        switch (cmd) {
        case F_GETFL:
            msg->arg2.mode = flags_ & (kStatusFlags | O_ACCMODE);
            return ZX_OK;
        case F_SETFL:
            flags_ = (flags_ & ~kStatusFlags) | (msg->arg2.mode & kStatusFlags);
            return ZX_OK;
        default:
            return ZX_ERR_NOT_SUPPORTED;
        }
    }
    case ZXRIO_READDIR: {
        if (arg > FDIO_CHUNK_SIZE) {
            return ZX_ERR_INVALID_ARGS;
        }
        if (msg->arg2.off == READDIR_CMD_RESET) {
            dircookie_.Reset();
        }
        zx_status_t r = vfs_->Readdir(vnode_.get(), &dircookie_, msg->data, arg);
        if (r >= 0) {
            msg->datalen = r;
        }
        return r;
    }
    case ZXRIO_IOCTL_1H: {
        if ((len > FDIO_IOCTL_MAX_INPUT) ||
            (arg > (ssize_t)sizeof(msg->data)) ||
            (IOCTL_KIND(msg->arg2.op) != IOCTL_KIND_SET_HANDLE)) {
            zx_handle_close(msg->handle[0]);
            return ZX_ERR_INVALID_ARGS;
        }
        if (len < sizeof(zx_handle_t)) {
            len = sizeof(zx_handle_t);
        }

        char in_buf[FDIO_IOCTL_MAX_INPUT];
        // The sending side copied the handle into msg->handle[0]
        // so that it would be sent via channel_write().  Here we
        // copy the local version back into the space in the buffer
        // that the original occupied.
        memcpy(in_buf, msg->handle, sizeof(zx_handle_t));
        memcpy(in_buf + sizeof(zx_handle_t), msg->data + sizeof(zx_handle_t),
               len - sizeof(zx_handle_t));

        switch (msg->arg2.op) {
        case IOCTL_VFS_MOUNT_FS:
        case IOCTL_VFS_MOUNT_MKDIR_FS:
            // Mounting requires ADMIN privileges
            if (!(flags_ & O_ADMIN)) {
                vfs_unmount_handle(msg->handle[0], 0);
                zx_handle_close(msg->handle[0]);
                return ZX_ERR_ACCESS_DENIED;
            }
            // If our permissions validate, fall through to the VFS ioctl
        }
        size_t actual = 0;
        zx_status_t status = vfs_->Ioctl(vnode_, msg->arg2.op,
                                         in_buf, len, msg->data, arg,
                                         &actual);
        if (status == ZX_ERR_NOT_SUPPORTED) {
            zx_handle_close(msg->handle[0]);
        }

        return status == ZX_OK ? static_cast<zx_status_t>(actual) : status;
    }
    case ZXRIO_IOCTL: {
        if (len > FDIO_IOCTL_MAX_INPUT ||
            (arg > (ssize_t)sizeof(msg->data)) ||
            (IOCTL_KIND(msg->arg2.op) == IOCTL_KIND_SET_HANDLE)) {
            return ZX_ERR_INVALID_ARGS;
        }
        char in_buf[FDIO_IOCTL_MAX_INPUT];
        memcpy(in_buf, msg->data, len);

        size_t actual = 0;
        switch (msg->arg2.op) {
        case IOCTL_VFS_GET_TOKEN: {
            // Ioctls which act on Connection
            if (arg != sizeof(zx_handle_t)) {
                return ZX_ERR_INVALID_ARGS;
            }
            zx::event returned_token;
            zx_status_t status = vfs_->VnodeToToken(vnode_, &token_, &returned_token);
            if (status == ZX_OK) {
                actual = sizeof(zx_handle_t);
                zx_handle_t* out = reinterpret_cast<zx_handle_t*>(msg->data);
                *out = returned_token.release();
            }
            break;
        }
        case IOCTL_VFS_UNMOUNT_NODE:
        case IOCTL_VFS_UNMOUNT_FS:
        case IOCTL_VFS_GET_DEVICE_PATH:
            // Unmounting ioctls require Connection privileges
            if (!(flags_ & O_ADMIN)) {
                return ZX_ERR_ACCESS_DENIED;
            }
        // If our permissions validate, fall through to the VFS ioctl
        default:
            zx_status_t status = vfs_->Ioctl(vnode_, msg->arg2.op,
                                             in_buf, len, msg->data, arg,
                                             &actual);
            if (status != ZX_OK) {
                return status;
            }
        }
        switch (IOCTL_KIND(msg->arg2.op)) {
        case IOCTL_KIND_DEFAULT:
            break;
        case IOCTL_KIND_GET_HANDLE:
            msg->hcount = 1;
            memcpy(msg->handle, msg->data, sizeof(zx_handle_t));
            break;
        case IOCTL_KIND_GET_TWO_HANDLES:
            msg->hcount = 2;
            memcpy(msg->handle, msg->data, 2 * sizeof(zx_handle_t));
            break;
        case IOCTL_KIND_GET_THREE_HANDLES:
            msg->hcount = 3;
            memcpy(msg->handle, msg->data, 3 * sizeof(zx_handle_t));
            break;
        }
        msg->arg2.off = 0;
        ZX_DEBUG_ASSERT(actual <= static_cast<size_t>(arg));
        msg->datalen = static_cast<uint32_t>(actual);
        return static_cast<uint32_t>(actual);
    }
    case ZXRIO_TRUNCATE: {
        if (!IsWritable(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }
        if (msg->arg2.off < 0) {
            return ZX_ERR_INVALID_ARGS;
        }
        return vnode_->Truncate(msg->arg2.off);
    }
    case ZXRIO_RENAME:
    case ZXRIO_LINK: {
        // Regardless of success or failure, we'll close the client-provided
        // vnode token handle.
        zx::event token(msg->handle[0]);

        if (len < 4) { // At least one byte for src + dst + null terminators
            return ZX_ERR_INVALID_ARGS;
        }

        char* data_end = (char*)(msg->data + len - 1);
        *data_end = '\0';
        const char* oldname = (const char*)msg->data;
        fbl::StringPiece oldStr(oldname, strlen(oldname));
        const char* newname = (const char*)msg->data + (oldStr.length() + 1);
        fbl::StringPiece newStr(newname, len - (oldStr.length() + 2));

        if (data_end <= newname) {
            return ZX_ERR_INVALID_ARGS;
        }

        switch (ZXRIO_OP(msg->op)) {
        case ZXRIO_RENAME:
            return vfs_->Rename(fbl::move(token), vnode_,
                                fbl::move(oldStr), fbl::move(newStr));
        case ZXRIO_LINK:
            return vfs_->Link(fbl::move(token), vnode_,
                              fbl::move(oldStr), fbl::move(newStr));
        }
        assert(false);
    }
    case ZXRIO_MMAP: {
        if (len != sizeof(zxrio_mmap_data_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        zxrio_mmap_data_t* data = reinterpret_cast<zxrio_mmap_data_t*>(msg->data);
        if ((flags_ & O_APPEND) && data->flags & FDIO_MMAP_FLAG_WRITE) {
            return ZX_ERR_ACCESS_DENIED;
        } else if (!IsWritable(flags_) && (data->flags & FDIO_MMAP_FLAG_WRITE)) {
            return ZX_ERR_ACCESS_DENIED;
        } else if (!IsReadable(flags_)) {
            return ZX_ERR_ACCESS_DENIED;
        }

        zx_status_t status = vnode_->Mmap(data->flags, data->length, &data->offset,
                                          &msg->handle[0]);
        if (status == ZX_OK) {
            msg->hcount = 1;
        }
        return status;
    }
    case ZXRIO_SYNC: {
        return vnode_->Sync();
    }
    case ZXRIO_UNLINK:
        return vfs_->Unlink(vnode_, fbl::StringPiece((const char*)msg->data, len));
    default:
        // close inbound handles so they do not leak
        for (unsigned i = 0; i < ZXRIO_HC(msg->op); i++) {
            zx_handle_close(msg->handle[i]);
        }
        return ZX_ERR_NOT_SUPPORTED;
    }
}

} // namespace fs
