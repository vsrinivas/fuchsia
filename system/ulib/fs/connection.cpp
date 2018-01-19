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
#include <fs/trace.h>
#include <fs/vnode.h>
#include <zircon/assert.h>

#define ZXDEBUG 0

namespace fs {
namespace {

void WriteDescribeError(zx::channel channel, zx_status_t status) {
    zxrio_describe_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.op = ZXRIO_ON_OPEN;
    msg.status = status;
    channel.write(0, &msg, sizeof(zxrio_describe_t), nullptr, 0);
}

void Describe(const fbl::RefPtr<Vnode>& vn, zxrio_describe_t* response,
              uint32_t flags) {
    response->op = ZXRIO_ON_OPEN;
    response->handle = ZX_HANDLE_INVALID;
    zx_status_t r;
    if (IsPathOnly(flags)) {
        r = vn->Vnode::GetHandles(flags, &response->handle, &response->type, &response->extra);
    } else {
        r = vn->GetHandles(flags, &response->handle, &response->type, &response->extra);
    }
    response->status = r;
}

// Performs a path walk and opens a connection to another node.
void OpenAt(Vfs* vfs, fbl::RefPtr<Vnode> parent,
            zxrio_msg_t* msg, zx::channel channel,
            fbl::StringPiece path, uint32_t flags, uint32_t mode) {
    // Filter out flags that are invalid when combined with REF_ONLY.
    if (IsPathOnly(flags)) {
        flags &= ZX_FS_FLAG_VNODE_REF_ONLY | ZX_FS_FLAG_DIRECTORY | ZX_FS_FLAG_DESCRIBE;
    }

    bool describe = flags & ZX_FS_FLAG_DESCRIBE;
    uint32_t open_flags = flags & (~ZX_FS_FLAG_DESCRIBE);

    fbl::RefPtr<Vnode> vnode;
    zx_status_t r = vfs->Open(fbl::move(parent), &vnode, path, &path, open_flags, mode);

    if (r != ZX_OK) {
        xprintf("vfs: open: r=%d\n", r);
    } else if (!(open_flags & ZX_FS_FLAG_NOREMOTE) && vnode->IsRemote()) {
        // Remote handoff to a remote filesystem node.
        zxrio_msg_t msg;
        memset(&msg, 0, ZXRIO_HDR_SZ);
        msg.op = ZXRIO_OPEN;
        msg.arg = flags;
        msg.arg2.mode = mode;
        msg.datalen = static_cast<uint32_t>(path.length());
        memcpy(msg.data, path.begin(), path.length());
        vfs->ForwardMessageRemote(fbl::move(vnode), fbl::move(channel), &msg);
        return;
    }

    if (describe) {
        // Regardless of the error code, in the 'describe' case, we
        // should respond to the client.
        if (r != ZX_OK) {
            WriteDescribeError(fbl::move(channel), r);
            return;
        }

        zxrio_describe_t response;
        memset(&response, 0, sizeof(response));
        Describe(vnode, &response, flags);
        uint32_t hcount = (response.handle != ZX_HANDLE_INVALID) ? 1 : 0;
        channel.write(0, &response, sizeof(zxrio_describe_t), &response.handle, hcount);
    } else if (r != ZX_OK) {
        return;
    }

    // If r == ZX_OK, then we hold a reference to vn from open.
    if (IsPathOnly(open_flags)) {
        vnode->Vnode::Serve(vfs, fbl::move(channel), open_flags);
    } else {
        vnode->Serve(vfs, fbl::move(channel), open_flags);
    }
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
            switch (status) {
            case ZX_OK:
                return ASYNC_WAIT_AGAIN;
            case ERR_DISPATCHER_ASYNC:
                return ASYNC_WAIT_FINISHED;
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
    return zxrio_handler(channel_.get(), &Connection::HandleMessageThunk, this);
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
        TRACE_DURATION("vfs", "ZXRIO_OPEN");
        char* path = (char*)msg->data;
        bool describe = arg & ZX_FS_FLAG_DESCRIBE;
        zx::channel channel(msg->handle[0]); // take ownership
        if ((len < 1) || (len > PATH_MAX)) {
            if (describe) {
                WriteDescribeError(fbl::move(channel), ZX_ERR_INVALID_ARGS);
            }
        } else if ((arg & ZX_FS_RIGHT_ADMIN) && !(flags_ & ZX_FS_RIGHT_ADMIN)) {
            if (describe) {
                WriteDescribeError(fbl::move(channel), ZX_ERR_ACCESS_DENIED);
            }
        } else {
            path[len] = 0;
            xprintf("vfs: open name='%s' flags=%d mode=%u\n", path, arg, msg->arg2.mode);
            OpenAt(vfs_, vnode_, msg, fbl::move(channel),
                   fbl::StringPiece(path, len), arg, msg->arg2.mode);
        }
        return ERR_DISPATCHER_INDIRECT;
    }
    case ZXRIO_CLOSE: {
        TRACE_DURATION("vfs", "ZXRIO_CLOSE");
        if (!IsPathOnly(flags_)) {
            return vnode_->Close();
        }
        return ZX_OK;
    }
    case ZXRIO_CLONE: {
        TRACE_DURATION("vfs", "ZXRIO_CLONE");
        zx::channel channel(msg->handle[0]); // take ownership
        fbl::RefPtr<Vnode> vn(vnode_);
        zx_status_t status = OpenVnode(flags_, &vn);
        bool describe = arg & ZX_FS_FLAG_DESCRIBE;
        if (describe) {
            zxrio_describe_t response;
            memset(&response, 0, sizeof(response));
            response.status = status;
            if (status == ZX_OK) {
                Describe(vnode_, &response, flags_);
            }
            uint32_t hcount = (response.handle != ZX_HANDLE_INVALID) ? 1 : 0;
            channel.write(0, &response, sizeof(zxrio_describe_t), &response.handle, hcount);
        }
        if (status == ZX_OK) {
            vn->Serve(vfs_, fbl::move(channel), flags_);
        }
        return ERR_DISPATCHER_INDIRECT;
    }
    case ZXRIO_READ: {
        TRACE_DURATION("vfs", "ZXRIO_READ");
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
        TRACE_DURATION("vfs", "ZXRIO_READ_AT");
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
        TRACE_DURATION("vfs", "ZXRIO_WRITE");
        if (!IsWritable(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }

        size_t actual;
        zx_status_t status;
        if (flags_ & ZX_FS_FLAG_APPEND) {
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
        TRACE_DURATION("vfs", "ZXRIO_WRITE_AT");
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
        TRACE_DURATION("vfs", "ZXRIO_SEEK");
        if (IsPathOnly(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }
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
        TRACE_DURATION("vfs", "ZXRIO_STAT");
        zx_status_t r;
        msg->datalen = sizeof(vnattr_t);
        if ((r = vnode_->Getattr((vnattr_t*)msg->data)) < 0) {
            return r;
        }
        return msg->datalen;
    }
    case ZXRIO_SETATTR: {
        TRACE_DURATION("vfs", "ZXRIO_SETATTR");
        // TODO(smklein): Prevent read-only files from setting attributes,
        // but allow attribute-setting on mutable directories.
        // For context: ZX-1262, ZX-1065
        if (IsPathOnly(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }
        zx_status_t r = vnode_->Setattr((vnattr_t*)msg->data);
        return r;
    }
    case ZXRIO_FCNTL: {
        TRACE_DURATION("vfs", "ZXRIO_FCNTL");
        uint32_t cmd = msg->arg;
        constexpr uint32_t kStatusFlags = ZX_FS_FLAG_APPEND;
        switch (cmd) {
        case F_GETFL:
            msg->arg2.mode = flags_ & (kStatusFlags | ZX_FS_RIGHTS | ZX_FS_FLAG_VNODE_REF_ONLY);
            return ZX_OK;
        case F_SETFL:
            flags_ = (flags_ & ~kStatusFlags) | (msg->arg2.mode & kStatusFlags);
            return ZX_OK;
        default:
            return ZX_ERR_NOT_SUPPORTED;
        }
    }
    case ZXRIO_READDIR: {
        TRACE_DURATION("vfs", "ZXRIO_READDIR");
        if (IsPathOnly(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }
        if (arg > FDIO_CHUNK_SIZE) {
            return ZX_ERR_INVALID_ARGS;
        }
        if (msg->arg2.off == READDIR_CMD_RESET) {
            dircookie_.Reset();
        }
        size_t actual;
        zx_status_t r = vfs_->Readdir(vnode_.get(), &dircookie_, msg->data, arg, &actual);
        if (r == ZX_OK) {
            msg->datalen = static_cast<uint32_t>(actual);
        }
        return r < 0 ? r : msg->datalen;
    }
    case ZXRIO_IOCTL_1H: {
        if (IsPathOnly(flags_)) {
            zx_handle_close(msg->handle[0]);
            return ZX_ERR_BAD_HANDLE;
        }
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
            if (!(flags_ & ZX_FS_RIGHT_ADMIN)) {
                vfs_unmount_handle(msg->handle[0], 0);
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
        if (IsPathOnly(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }
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
            if (!(flags_ & ZX_FS_RIGHT_ADMIN)) {
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
        TRACE_DURATION("vfs", "ZXRIO_TRUNCATE");
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
        TRACE_DURATION("vfs", (ZXRIO_OP(msg->op) == ZXRIO_RENAME ?
                               "ZXRIO_RENAME" : "ZXRIO_LINK"));
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
        case ZXRIO_RENAME: {
            return vfs_->Rename(fbl::move(token), vnode_,
                                fbl::move(oldStr), fbl::move(newStr));
        }
        case ZXRIO_LINK: {
            return vfs_->Link(fbl::move(token), vnode_,
                              fbl::move(oldStr), fbl::move(newStr));
        }
        }
        assert(false);
    }
    case ZXRIO_MMAP: {
        TRACE_DURATION("vfs", "ZXRIO_MMAP");
        if (IsPathOnly(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }
        if (len != sizeof(zxrio_mmap_data_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        zxrio_mmap_data_t* data = reinterpret_cast<zxrio_mmap_data_t*>(msg->data);
        if ((flags_ & ZX_FS_FLAG_APPEND) && data->flags & FDIO_MMAP_FLAG_WRITE) {
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
        TRACE_DURATION("vfs", "ZXRIO_SYNC");
        if (IsPathOnly(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }
        zx_txid_t txid = msg->txid;
        Vnode::SyncCallback closure([this, txid](zx_status_t status) {
            zxrio_msg_t msg;
            memset(&msg, 0, ZXRIO_HDR_SZ);
            msg.txid = txid;
            msg.op = ZXRIO_STATUS;
            msg.arg = status;
            zxrio_respond(channel_.get(), &msg);

            // Reset the wait object
            ZX_ASSERT(wait_.Begin(vfs_->async()) == ZX_OK);
        });

        vnode_->Sync(fbl::move(closure));
        return ERR_DISPATCHER_ASYNC;
    }
    case ZXRIO_UNLINK: {
        TRACE_DURATION("vfs", "ZXRIO_UNLINK");
        return vfs_->Unlink(vnode_, fbl::StringPiece((const char*)msg->data, len));
    }
    default:
        // close inbound handles so they do not leak
        for (unsigned i = 0; i < ZXRIO_HC(msg->op); i++) {
            zx_handle_close(msg->handle[i]);
        }
        return ZX_ERR_NOT_SUPPORTED;
    }
}

} // namespace fs
