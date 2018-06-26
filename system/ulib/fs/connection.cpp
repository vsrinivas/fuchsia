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

#include <lib/fdio/debug.h>
#include <lib/fdio/io.fidl.h>
#include <lib/fdio/io.h>
#include <lib/fdio/remoteio.h>
#include <lib/fdio/vfs.h>
#include <fs/trace.h>
#include <fs/vnode.h>
#include <zircon/assert.h>
#include <lib/zx/handle.h>

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

void Describe(const fbl::RefPtr<Vnode>& vn, uint32_t flags,
              zxrio_describe_t* response, zx_handle_t* handle) {
    response->op = ZXRIO_ON_OPEN;
    zx_status_t r;
    *handle = ZX_HANDLE_INVALID;
    if (IsPathOnly(flags)) {
        r = vn->Vnode::GetHandles(flags, handle, &response->extra.tag, &response->extra);
    } else {
        r = vn->GetHandles(flags, handle, &response->extra.tag, &response->extra);
    }

    // If a handle was returned, encode it.
    if (*handle != ZX_HANDLE_INVALID) {
        response->extra.handle = FIDL_HANDLE_PRESENT;
    } else {
        response->extra.handle = FIDL_HANDLE_ABSENT;
    }

    // If a valid response was returned, encode it.
    response->status = r;
    response->extra_ptr = reinterpret_cast<zxrio_object_info_t*>(r == ZX_OK ?
                                                                 FIDL_ALLOC_PRESENT :
                                                                 FIDL_ALLOC_ABSENT);
}

// Performs a path walk and opens a connection to another node.
void OpenAt(Vfs* vfs, fbl::RefPtr<Vnode> parent, zx::channel channel,
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
#ifdef ZXRIO_FIDL
        fuchsia_io_DirectoryOpenRequest* request =
            reinterpret_cast<fuchsia_io_DirectoryOpenRequest*>(&msg);
        memset(request, 0, sizeof(fuchsia_io_DirectoryOpenRequest));
        request->hdr.ordinal = ZXFIDL_OPEN;
        request->flags = flags;
        request->mode = mode;
        request->path.size = path.length();
        request->path.data = (char*) FIDL_ALLOC_PRESENT;
        request->object = FIDL_HANDLE_PRESENT;
        void* secondary =
                reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(request) +
                                        FIDL_ALIGN(sizeof(fuchsia_io_DirectoryOpenRequest)));
        memcpy(secondary, path.begin(), path.length());
#else
        memset(&msg, 0, ZXRIO_HDR_SZ);
        msg.op = ZXRIO_OPEN;
        msg.arg = flags;
        msg.arg2.mode = mode;
        msg.datalen = static_cast<uint32_t>(path.length());
        memcpy(msg.data, path.begin(), path.length());
#endif
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
        zx_handle_t extra = ZX_HANDLE_INVALID;
        Describe(vnode, flags, &response, &extra);
        uint32_t hcount = (extra != ZX_HANDLE_INVALID) ? 1 : 0;
        channel.write(0, &response, sizeof(zxrio_describe_t), &extra, hcount);
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

constexpr zx_signals_t kWakeSignals = ZX_CHANNEL_READABLE |
                                      ZX_CHANNEL_PEER_CLOSED | kLocalTeardownSignal;

Connection::Connection(Vfs* vfs, fbl::RefPtr<Vnode> vnode,
                       zx::channel channel, uint32_t flags)
    : vfs_(vfs), vnode_(fbl::move(vnode)), channel_(fbl::move(channel)),
      wait_(this, ZX_HANDLE_INVALID, kWakeSignals), flags_(flags) {
    ZX_DEBUG_ASSERT(vfs);
    ZX_DEBUG_ASSERT(vnode_);
    ZX_DEBUG_ASSERT(channel_);
}

Connection::~Connection() {
    // Stop waiting and clean up if still connected.
    if (wait_.is_pending()) {
        zx_status_t status = wait_.Cancel();
        ZX_DEBUG_ASSERT_MSG(status == ZX_OK, "Could not cancel wait: status=%d", status);
    }

    // Invoke a "close" call to the underlying object if we haven't already.
    if (is_open()) {
        CallClose();
    }

    // Release the token associated with this connection's vnode since the connection
    // will be releasing the vnode's reference once this function returns.
    if (token_) {
        vfs_->TokenDiscard(fbl::move(token_));
    }
}

void Connection::AsyncTeardown() {
    if (channel_) {
        ZX_ASSERT(channel_.signal(0, kLocalTeardownSignal) == ZX_OK);
    }
}

void Connection::SyncTeardown() {
    if (wait_.Cancel() == ZX_OK) {
        Terminate(/* call_close= */ true);
    }
}

zx_status_t Connection::Serve() {
    wait_.set_object(channel_.get());
    return wait_.Begin(vfs_->async());
}

void Connection::HandleSignals(async_t* async, async::WaitBase* wait, zx_status_t status,
                               const zx_packet_signal_t* signal) {
    ZX_DEBUG_ASSERT(is_open());

    if (status == ZX_OK) {
        if (vfs_->IsTerminating()) {
            // Short-circuit locally destroyed connections, rather than servicing
            // requests on their behalf. This prevents new requests from being
            // opened while filesystems are torn down.
            status = ZX_ERR_PEER_CLOSED;
        } else if (signal->observed & ZX_CHANNEL_READABLE) {
            // Handle the message.
            status = CallHandler();
            switch (status) {
            case ERR_DISPATCHER_ASYNC:
                return;
            case ZX_OK:
                status = wait_.Begin(async);
                if (status == ZX_OK) {
                    return;
                }
                break;
            }
        }
    }

    bool call_close = (status != ERR_DISPATCHER_DONE);
    Terminate(call_close);
}

void Connection::Terminate(bool call_close) {
    if (call_close) {
        // Give the dispatcher a chance to clean up.
        CallClose();
    } else {
        // It's assumed that someone called the close handler
        // prior to calling this function.
        set_closed();
    }

    // Tell the VFS that the connection closed remotely.
    // This might have the side-effect of destroying this object.
    vfs_->OnConnectionClosedRemotely(this);
}

zx_status_t Connection::CallHandler() {
    return zxrio_handler(channel_.get(), &Connection::HandleMessageThunk, this);
}

void Connection::CallClose() {
    channel_.reset();
    CallHandler();
    set_closed();
}

zx_status_t Connection::HandleMessageThunk(zxrio_msg_t* msg, void* cookie) {
    Connection* connection = static_cast<Connection*>(cookie);
    return connection->HandleMessage(msg);
}

// Flags which can be modified by SetFlags
constexpr uint32_t kStatusFlags = ZX_FS_FLAG_APPEND;

zx_status_t Connection::HandleMessage(zxrio_msg_t* msg) {
    uint32_t len = msg->datalen;
    int32_t arg = msg->arg;

    if (!ZXRIO_FIDL_MSG(msg->op)) {
        msg->datalen = 0;
        msg->hcount = 0;
    }

    switch (ZXRIO_OP(msg->op)) {
    case ZXFIDL_OPEN:
    case ZXRIO_OPEN: {
        TRACE_DURATION("vfs", "ZXRIO_OPEN");
        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        auto request = reinterpret_cast<fuchsia_io_DirectoryOpenRequest*>(msg);

        uint32_t flags;
        uint32_t mode;
        char* path;
        zx::channel channel;
        if (fidl) {
            flags = request->flags;
            mode = request->mode;
            path = request->path.data;
            len = static_cast<uint32_t>(request->path.size);
            channel.reset(request->object);
        } else {
            flags = arg;
            mode = msg->arg2.mode;
            path = (char*) msg->data;
            channel.reset(msg->handle[0]);
        }
        bool describe = flags & ZX_FS_FLAG_DESCRIBE;
        if ((len < 1) || (len > PATH_MAX)) {
            if (describe) {
                WriteDescribeError(fbl::move(channel), ZX_ERR_INVALID_ARGS);
            }
        } else if ((flags & ZX_FS_RIGHT_ADMIN) && !(flags_ & ZX_FS_RIGHT_ADMIN)) {
            if (describe) {
                WriteDescribeError(fbl::move(channel), ZX_ERR_ACCESS_DENIED);
            }
        } else {
            path[len] = 0;
            xprintf("vfs: open name='%s' flags=%d mode=%u\n", path, flags, mode);
            OpenAt(vfs_, vnode_, fbl::move(channel),
                   fbl::StringPiece(path, len), flags, mode);
        }
        return ERR_DISPATCHER_INDIRECT;
    }
    case ZXFIDL_CLOSE:
    case ZXRIO_CLOSE: {
        TRACE_DURATION("vfs", "ZXRIO_CLOSE");
        if (!IsPathOnly(flags_)) {
            return vnode_->Close();
        }
        return ZX_OK;
    }
    case ZXFIDL_CLONE:
    case ZXRIO_CLONE: {
        TRACE_DURATION("vfs", "ZXRIO_CLONE");
        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        auto request = reinterpret_cast<fuchsia_io_ObjectCloneRequest*>(msg);

        zx::channel channel;
        uint32_t flags;

        if (fidl) {
            channel.reset(request->object);
            flags = request->flags;
        } else {
            channel.reset(msg->handle[0]); // take ownership
            flags = arg;
        }
        fbl::RefPtr<Vnode> vn(vnode_);
        zx_status_t status = OpenVnode(flags_, &vn);
        bool describe = flags & ZX_FS_FLAG_DESCRIBE;
        if (describe) {
            zxrio_describe_t response;
            memset(&response, 0, sizeof(response));
            response.status = status;
            zx_handle_t extra = ZX_HANDLE_INVALID;
            if (status == ZX_OK) {
                Describe(vnode_, flags_, &response, &extra);
            }
            uint32_t hcount = (extra != ZX_HANDLE_INVALID) ? 1 : 0;
            channel.write(0, &response, sizeof(zxrio_describe_t), &extra, hcount);
        }
        if (status == ZX_OK) {
            vn->Serve(vfs_, fbl::move(channel), flags_);
        }
        return ERR_DISPATCHER_INDIRECT;
    }
    case ZXFIDL_READ:
    case ZXRIO_READ: {
        TRACE_DURATION("vfs", "ZXRIO_READ");
        if (!IsReadable(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }
        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        auto request = reinterpret_cast<fuchsia_io_FileReadRequest*>(msg);
        auto response = reinterpret_cast<fuchsia_io_FileReadResponse*>(msg);
        void* data;
        if (fidl) {
            data = (void*)((uintptr_t)response + FIDL_ALIGN(sizeof(fuchsia_io_FileReadResponse)));
            len = static_cast<uint32_t>(request->count);
        } else {
            data = msg->data;
            len = arg;
        }
        size_t actual;
        zx_status_t status = vnode_->Read(data, len, offset_, &actual);
        if (status == ZX_OK) {
            ZX_DEBUG_ASSERT(actual <= static_cast<size_t>(len));
            offset_ += actual;
            if (fidl) {
                response->data.count = actual;
            } else {
                msg->datalen = static_cast<uint32_t>(actual);
                status = static_cast<zx_status_t>(actual);
            }
        }
        return status;
    }
    case ZXFIDL_READ_AT:
    case ZXRIO_READ_AT: {
        TRACE_DURATION("vfs", "ZXRIO_READ_AT");
        if (!IsReadable(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }
        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        auto request = reinterpret_cast<fuchsia_io_FileReadAtRequest*>(msg);
        auto response = reinterpret_cast<fuchsia_io_FileReadAtResponse*>(msg);
        void* data;
        uint64_t offset;
        if (fidl) {
            data = (void*)((uintptr_t)response + FIDL_ALIGN(sizeof(fuchsia_io_FileReadAtResponse)));
            len = static_cast<uint32_t>(request->count);
            offset = request->offset;
        } else {
            data = msg->data;
            len = arg;
            offset = msg->arg2.off;
        }

        size_t actual;
        zx_status_t status = vnode_->Read(data, len, offset, &actual);
        if (status == ZX_OK) {
            ZX_DEBUG_ASSERT(actual <= static_cast<size_t>(len));
            if (fidl) {
                response->data.count = actual;
            } else {
                msg->datalen = static_cast<uint32_t>(actual);
                status = static_cast<zx_status_t>(actual);
            }
        }
        return status;
    }
    case ZXFIDL_WRITE:
    case ZXRIO_WRITE: {
        TRACE_DURATION("vfs", "ZXRIO_WRITE");
        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        fuchsia_io_FileWriteRequest* request = reinterpret_cast<fuchsia_io_FileWriteRequest*>(msg);
        fuchsia_io_FileWriteResponse* response = reinterpret_cast<fuchsia_io_FileWriteResponse*>(msg);
        void* data;
        if (fidl) {
            data = request->data.data;
            len = static_cast<uint32_t>(request->data.count);
        } else {
            data = msg->data;
        }

        if (!IsWritable(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }

        size_t actual = 0;
        zx_status_t status;
        if (flags_ & ZX_FS_FLAG_APPEND) {
            size_t end;
            status = vnode_->Append(data, len, &end, &actual);
            if (status == ZX_OK) {
                offset_ = end;
            }
        } else {
            status = vnode_->Write(data, len, offset_, &actual);
            if (status == ZX_OK) {
                offset_ += actual;
            }
        }
        ZX_DEBUG_ASSERT(actual <= static_cast<size_t>(len));
        if (fidl) {
            response->actual = actual;
            return status;
        } else {
            return status == ZX_OK ? static_cast<zx_status_t>(actual) : status;
        }
    }
    case ZXFIDL_WRITE_AT:
    case ZXRIO_WRITE_AT: {
        TRACE_DURATION("vfs", "ZXRIO_WRITE_AT");
        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        fuchsia_io_FileWriteAtRequest* request =
            reinterpret_cast<fuchsia_io_FileWriteAtRequest*>(msg);
        fuchsia_io_FileWriteAtResponse* response =
            reinterpret_cast<fuchsia_io_FileWriteAtResponse*>(msg);
        void* data;
        uint64_t offset;
        if (fidl) {
            data = request->data.data;
            len = static_cast<uint32_t>(request->data.count);
            offset = request->offset;
        } else {
            data = msg->data;
            offset = msg->arg2.off;
        }
        if (!IsWritable(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }
        size_t actual = 0;
        zx_status_t status = vnode_->Write(data, len, offset, &actual);
        ZX_DEBUG_ASSERT(actual <= static_cast<size_t>(len));
        if (fidl) {
            response->actual = actual;
            return status;
        } else {
            return status == ZX_OK ? static_cast<zx_status_t>(actual) : status;
        }
    }
    case ZXFIDL_SEEK:
    case ZXRIO_SEEK: {
        TRACE_DURATION("vfs", "ZXRIO_SEEK");
        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        fuchsia_io_FileSeekRequest* request = reinterpret_cast<fuchsia_io_FileSeekRequest*>(msg);
        fuchsia_io_FileSeekResponse* response = reinterpret_cast<fuchsia_io_FileSeekResponse*>(msg);

        static_assert(SEEK_SET == fuchsia_io_SeekOrigin_Start, "");
        static_assert(SEEK_CUR == fuchsia_io_SeekOrigin_Current, "");
        static_assert(SEEK_END == fuchsia_io_SeekOrigin_End, "");
        off_t offset;
        int whence;
        if (fidl) {
            offset = request->offset;
            whence = request->start;
        } else {
            offset = msg->arg2.off;
            whence = arg;
        }

        if (IsPathOnly(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }
        vnattr_t attr;
        zx_status_t r;
        if ((r = vnode_->Getattr(&attr)) < 0) {
            return r;
        }
        size_t n;
        switch (whence) { case SEEK_SET:
            if (offset < 0) {
                return ZX_ERR_INVALID_ARGS;
            }
            n = offset;
            break;
        case SEEK_CUR:
            n = offset_ + offset;
            if (offset < 0) {
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
            n = attr.size + offset;
            if (offset < 0) {
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
        if (fidl) {
            response->offset = offset_;
        } else {
            msg->arg2.off = offset_;
        }
        return ZX_OK;
    }
    case ZXFIDL_STAT:
    case ZXRIO_STAT: {
        TRACE_DURATION("vfs", "ZXRIO_STAT");
        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        auto response = reinterpret_cast<fuchsia_io_NodeGetAttrResponse*>(msg);

        // TODO(smklein): Consider using "NodeAttributes" within
        // ulib/fs, rather than vnattr_t.
        // Alternatively modify vnattr_t to match "NodeAttributes"
        vnattr_t attr;
        zx_status_t r;
        if ((r = vnode_->Getattr(&attr)) != ZX_OK) {
            return r;
        }

        if (fidl) {
            response->attributes.mode = attr.mode;
            response->attributes.id = attr.inode;
            response->attributes.content_size = attr.size;
            response->attributes.storage_size = attr.blksize * attr.blkcount;
            response->attributes.link_count = attr.nlink;
            response->attributes.creation_time = attr.create_time;
            response->attributes.modification_time = attr.modify_time;
            return r;
        }
        memcpy(msg->data, &attr, sizeof(vnattr_t));
        msg->datalen = sizeof(vnattr_t);
        return msg->datalen;
    }
    case ZXFIDL_SETATTR:
    case ZXRIO_SETATTR: {
        TRACE_DURATION("vfs", "ZXRIO_SETATTR");
        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        auto request = reinterpret_cast<fuchsia_io_NodeSetAttrRequest*>(msg);

        // TODO(smklein): Prevent read-only files from setting attributes,
        // but allow attribute-setting on mutable directories.
        // For context: ZX-1262, ZX-1065
        if (IsPathOnly(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }

        vnattr_t attr;
        if (fidl) {
            attr.valid = request->flags;
            attr.create_time = request->attributes.creation_time;
            attr.modify_time = request->attributes.modification_time;
        } else {
            memcpy(&attr, &msg->data, sizeof(attr));
        }

        return vnode_->Setattr(&attr);
    }
    case ZXFIDL_GET_FLAGS: {
        TRACE_DURATION("vfs", "ZXFIDL_GET_FLAGS");
        fuchsia_io_FileGetFlagsResponse* response =
            reinterpret_cast<fuchsia_io_FileGetFlagsResponse*>(msg);
        response->flags = flags_ & (kStatusFlags | ZX_FS_RIGHTS | ZX_FS_FLAG_VNODE_REF_ONLY);
        return ZX_OK;
    }
    case ZXFIDL_SET_FLAGS: {
        TRACE_DURATION("vfs", "ZXFIDL_SET_FLAGS");
        fuchsia_io_FileSetFlagsRequest* request =
            reinterpret_cast<fuchsia_io_FileSetFlagsRequest*>(msg);
        flags_ = (flags_ & ~kStatusFlags) | (request->flags & kStatusFlags);
        return ZX_OK;
    }
    case ZXRIO_FCNTL: {
        TRACE_DURATION("vfs", "ZXRIO_FCNTL");
        uint32_t cmd = msg->arg;
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
    case ZXFIDL_REWIND: {
        TRACE_DURATION("vfs", "ZXRIO_REWIND");
        if (IsPathOnly(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }
        dircookie_.Reset();
        return ZX_OK;
    }
    case ZXFIDL_READDIR:
    case ZXRIO_READDIR: {
        TRACE_DURATION("vfs", "ZXRIO_READDIR");
        if (IsPathOnly(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }

        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        auto request = reinterpret_cast<fuchsia_io_DirectoryReadDirentsRequest*>(msg);
        auto response = reinterpret_cast<fuchsia_io_DirectoryReadDirentsResponse*>(msg);
        uint32_t max_out;
        void* data;
        if (fidl) {
            data = (void*)((uintptr_t)response +
                    FIDL_ALIGN(sizeof(fuchsia_io_DirectoryReadDirentsResponse)));
            max_out = static_cast<uint32_t>(request->max_out);
        } else {
            max_out = arg;
            if (msg->arg2.off == READDIR_CMD_RESET) {
                dircookie_.Reset();
            }
            data = msg->data;
        }

        if (max_out > FDIO_CHUNK_SIZE) {
            return ZX_ERR_INVALID_ARGS;
        }
        size_t actual;
        zx_status_t r = vfs_->Readdir(vnode_.get(), &dircookie_, data, max_out, &actual);
        if (r == ZX_OK) {
            if (fidl) {
                response->dirents.count = actual;
            } else {
                msg->datalen = static_cast<uint32_t>(actual);
                r = static_cast<zx_status_t>(actual);
            }
        }
        return r;
    }
    case ZXFIDL_IOCTL:
    case ZXRIO_IOCTL:
    case ZXRIO_IOCTL_1H: {
        auto request = reinterpret_cast<fuchsia_io_NodeIoctlRequest*>(msg);
        auto response = reinterpret_cast<fuchsia_io_NodeIoctlResponse*>(msg);

        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        uint32_t op;
        zx_handle_t* handles;
        size_t hcount;
        void* in;
        size_t inlen;
        void* out;
        size_t outlen;
        void* secondary = (void*)((uintptr_t)(msg) +
                FIDL_ALIGN(sizeof(fuchsia_io_NodeIoctlResponse)));
        if (fidl) {
            op = request->opcode;
            handles = static_cast<zx_handle_t*>(request->handles.data);
            hcount = request->handles.count;
            in = request->in.data;
            inlen = request->in.count;
            out = secondary;
            outlen = request->max_out;
        } else {
            op = msg->arg2.op;
            handles = msg->handle;
            hcount = ZXRIO_OP(msg->op) == ZXRIO_IOCTL_1H ? 1 : 0;
            in = msg->data;
            inlen = len;
            out = msg->data;
            outlen = arg;
        }

        zx::handle handle;
        if (hcount == 1) {
            handle.reset(handles[0]);
            if (IOCTL_KIND(op) != IOCTL_KIND_SET_HANDLE) {
                return ZX_ERR_INVALID_ARGS;
            }
            if (inlen < sizeof(zx_handle_t)) {
                inlen = sizeof(zx_handle_t);
            }
        }
        if (IsPathOnly(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }
        if ((inlen > FDIO_IOCTL_MAX_INPUT) || (outlen > FDIO_IOCTL_MAX_INPUT)) {
            return ZX_ERR_INVALID_ARGS;
        }

        char in_buf[FDIO_IOCTL_MAX_INPUT];
        // The sending side copied the handle into msg->handle[0]
        // so that it would be sent via channel_write().  Here we
        // copy the local version back into the space in the buffer
        // that the original occupied.
        size_t hsize = hcount * sizeof(zx_handle_t);
        zx_handle_t h = handle.release();
        memcpy(in_buf, &h, hsize);
        memcpy(in_buf + hsize, (void*)((uintptr_t)in + hsize), inlen - hsize);

        // Some ioctls operate on the connection only, and don't
        // require a call to Vfs::Ioctl
        bool do_ioctl = true;
        size_t actual = 0;
        zx_status_t status;
        switch (op) {
        case IOCTL_VFS_MOUNT_FS:
        case IOCTL_VFS_MOUNT_MKDIR_FS:
            // Mounting requires ADMIN privileges
            if (!(flags_ & ZX_FS_RIGHT_ADMIN)) {
                vfs_unmount_handle(h, 0);
                return ZX_ERR_ACCESS_DENIED;
            }
            // If our permissions validate, fall through to the VFS ioctl
            break;
        case IOCTL_VFS_GET_TOKEN: {
            // Ioctls which act on Connection
            if (outlen != sizeof(zx_handle_t)) {
                return ZX_ERR_INVALID_ARGS;
            }
            zx::event returned_token;
            status = vfs_->VnodeToToken(vnode_, &token_, &returned_token);
            if (status == ZX_OK) {
                actual = sizeof(zx_handle_t);
                zx_handle_t* handleout = reinterpret_cast<zx_handle_t*>(out);
                *handleout = returned_token.release();
            }
            do_ioctl = false;
            break;
        }
        case IOCTL_VFS_UNMOUNT_FS: {
            // Unmounting ioctls require Connection privileges
            if (!(flags_ & ZX_FS_RIGHT_ADMIN)) {
                return ZX_ERR_ACCESS_DENIED;
            }
            bool fidl = ZXRIO_FIDL_MSG(msg->op);
            zx_txid_t txid = msg->txid;

            // "IOCTL_VFS_UNMOUNT_FS" is fatal to the requesting connections.
            Vfs::ShutdownCallback closure([ch = fbl::move(channel_), txid, fidl]
                                           (zx_status_t status) {
                zxrio_msg_t msg;
                memset(&msg, 0, sizeof(msg));
                msg.txid = txid;
                msg.op = fidl ? ZXFIDL_IOCTL : ZXRIO_IOCTL;
                zxrio_write_response(ch.get(), status, &msg);
            });
            Vfs* vfs = vfs_;
            Terminate(/* call_close= */ true);
            vfs->Shutdown(fbl::move(closure));
            return ERR_DISPATCHER_ASYNC;
        }
        case IOCTL_VFS_UNMOUNT_NODE:
        case IOCTL_VFS_GET_DEVICE_PATH:
            // Unmounting ioctls require Connection privileges
            if (!(flags_ & ZX_FS_RIGHT_ADMIN)) {
                return ZX_ERR_ACCESS_DENIED;
            }
            // If our permissions validate, fall through to the VFS ioctl
            break;
        }

        if (do_ioctl) {
            status = vfs_->Ioctl(vnode_, op, in_buf, inlen, out, outlen,
                                 &actual);
            if (status == ZX_ERR_NOT_SUPPORTED && hcount > 0) {
                zx_handle_close(h);
            }
        }

        if (status != ZX_OK) {
            return status;
        }

        hcount = 0;
        switch (IOCTL_KIND(op)) {
        case IOCTL_KIND_DEFAULT:
            break;
        case IOCTL_KIND_GET_HANDLE:
            hcount = 1;
            break;
        case IOCTL_KIND_GET_TWO_HANDLES:
            hcount = 2;
            break;
        case IOCTL_KIND_GET_THREE_HANDLES:
            hcount = 3;
            break;
        }

        ZX_DEBUG_ASSERT(actual <= static_cast<size_t>(outlen));
        if (fidl) {
            response->handles.count = hcount;
            response->handles.data = secondary;
            response->out.count = actual;
            response->out.data = secondary;
            return ZX_OK;
        } else {
            msg->hcount = static_cast<uint32_t>(hcount);
            memcpy(msg->handle, msg->data, hcount * sizeof(zx_handle_t));
            msg->datalen = static_cast<uint32_t>(actual);
            return static_cast<zx_status_t>(actual);
        }
    }
    case ZXFIDL_TRUNCATE:
    case ZXRIO_TRUNCATE: {
        TRACE_DURATION("vfs", "ZXRIO_TRUNCATE");
        if (!IsWritable(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }

        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        auto request = reinterpret_cast<fuchsia_io_FileTruncateRequest*>(msg);
        uint64_t length;
        if (fidl) {
            length = request->length;
        } else {
            length = msg->arg2.off;
        }

        return vnode_->Truncate(length);
    }
    case ZXFIDL_RENAME:
    case ZXFIDL_LINK:
    case ZXRIO_RENAME:
    case ZXRIO_LINK: {
        TRACE_DURATION("vfs", (ZXRIO_OP(msg->op) == ZXRIO_RENAME ?
                               "ZXRIO_RENAME" : "ZXRIO_LINK"));
        bool fidl = ZXRIO_FIDL_MSG(msg->op);

        // These static assertions must all validate before fuchsia_io_DirectoryRenameRequest
        // and fuchsia_io_DirectoryLinkRequest can be used interchangeably
        static_assert(sizeof(fuchsia_io_DirectoryRenameRequest) ==
                sizeof(fuchsia_io_DirectoryLinkRequest), "");
        static_assert(sizeof(fuchsia_io_DirectoryRenameResponse) ==
                sizeof(fuchsia_io_DirectoryLinkResponse), "");
        static_assert(offsetof(fuchsia_io_DirectoryRenameRequest, src) ==
                offsetof(fuchsia_io_DirectoryLinkRequest, src), "");
        static_assert(offsetof(fuchsia_io_DirectoryRenameRequest, dst_parent_token) ==
                      offsetof(fuchsia_io_DirectoryLinkRequest, dst_parent_token), "");
        static_assert(offsetof(fuchsia_io_DirectoryRenameRequest, dst) ==
                offsetof(fuchsia_io_DirectoryLinkRequest, dst), "");
        auto request = reinterpret_cast<fuchsia_io_DirectoryRenameRequest*>(msg);

        // Regardless of success or failure, we'll close the client-provided
        // vnode token handle.
        zx::event token;
        fbl::StringPiece oldStr, newStr;

        if (fidl) {
            token.reset(request->dst_parent_token);
            if (request->src.size < 1 || request->dst.size < 1) {
                return ZX_ERR_INVALID_ARGS;
            }
            oldStr.set(request->src.data, request->src.size);
            newStr.set(request->dst.data, request->dst.size);
        } else {
            token.reset(msg->handle[0]);
            if (len < 4) { // At least one byte for src + dst + null terminators
                return ZX_ERR_INVALID_ARGS;
            }

            char* data_end = (char*)(msg->data + len - 1);
            *data_end = '\0';
            const char* oldname = (const char*)msg->data;
            oldStr.set(oldname, strlen(oldname));
            const char* newname = (const char*)msg->data + (oldStr.length() + 1);
            newStr.set(newname, len - (oldStr.length() + 2));

            if (data_end <= newname) {
                return ZX_ERR_INVALID_ARGS;
            }
        }

        switch (ZXRIO_OP(msg->op)) {
        case ZXFIDL_RENAME:
        case ZXRIO_RENAME: {
            return vfs_->Rename(fbl::move(token), vnode_,
                                fbl::move(oldStr), fbl::move(newStr));
        }
        case ZXFIDL_LINK:
        case ZXRIO_LINK: {
            return vfs_->Link(fbl::move(token), vnode_,
                              fbl::move(oldStr), fbl::move(newStr));
        }
        }
        __builtin_trap();
    }
    case ZXFIDL_GET_VMO:
    case ZXRIO_MMAP: {
        TRACE_DURATION("vfs", "ZXRIO_MMAP");
        if (IsPathOnly(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }
        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        uint32_t flags;
        zx_handle_t* handle;

        if (fidl) {
            auto request = reinterpret_cast<fuchsia_io_FileGetVmoRequest*>(msg);
            auto response = reinterpret_cast<fuchsia_io_FileGetVmoResponse*>(msg);
            flags = request->flags;
            handle = &response->vmo;
        } else {
            if (len != sizeof(zxrio_mmap_data_t)) {
                return ZX_ERR_INVALID_ARGS;
            }
            zxrio_mmap_data_t* data = reinterpret_cast<zxrio_mmap_data_t*>(msg->data);
            flags = data->flags;
            handle = &msg->handle[0];
        }

        if ((flags & FDIO_MMAP_FLAG_PRIVATE) && (flags & FDIO_MMAP_FLAG_EXACT)) {
            return ZX_ERR_INVALID_ARGS;
        } else if ((flags_ & ZX_FS_FLAG_APPEND) && flags & FDIO_MMAP_FLAG_WRITE) {
            return ZX_ERR_ACCESS_DENIED;
        } else if (!IsWritable(flags_) && (flags & FDIO_MMAP_FLAG_WRITE)) {
            return ZX_ERR_ACCESS_DENIED;
        } else if (!IsReadable(flags_)) {
            return ZX_ERR_ACCESS_DENIED;
        }

        zx_status_t status = vnode_->GetVmo(flags, handle);
        if (!fidl && status == ZX_OK) {
            msg->hcount = 1;
        }
        return status;
    }
    case ZXFIDL_SYNC:
    case ZXRIO_SYNC: {
        TRACE_DURATION("vfs", "ZXRIO_SYNC");
        if (IsPathOnly(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }
        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        zx_txid_t txid = msg->txid;
        Vnode::SyncCallback closure([this, txid, fidl] (zx_status_t status) {
            zxrio_msg_t msg;
            memset(&msg, 0, ZXRIO_HDR_SZ);
            msg.txid = txid;
            msg.op = fidl ? ZXFIDL_SYNC : ZXRIO_SYNC;
            zxrio_write_response(channel_.get(), status, &msg);

            // Try to reset the wait object
            ZX_ASSERT_MSG(wait_.Begin(vfs_->async()) == ZX_OK, "Dispatch loop unexpectedly ended");
        });

        vnode_->Sync(fbl::move(closure));
        return ERR_DISPATCHER_ASYNC;
    }
    case ZXFIDL_UNLINK:
    case ZXRIO_UNLINK: {
        TRACE_DURATION("vfs", "ZXRIO_UNLINK");
        bool fidl = ZXRIO_FIDL_MSG(msg->op);
        fuchsia_io_DirectoryUnlinkRequest* request =
            reinterpret_cast<fuchsia_io_DirectoryUnlinkRequest*>(msg);
        char* data;
        uint32_t datalen;
        if (fidl) {
            data = request->path.data;
            datalen = static_cast<uint32_t>(request->path.size);
        } else {
            data = reinterpret_cast<char*>(msg->data);
            datalen = len;
        }
        return vfs_->Unlink(vnode_, fbl::StringPiece(data, datalen));
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
