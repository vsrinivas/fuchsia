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

#include <fs/trace.h>
#include <fs/vnode.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/debug.h>
#include <lib/fdio/io.h>
#include <lib/fdio/remoteio.h>
#include <lib/fdio/vfs.h>
#include <lib/zx/handle.h>
#include <zircon/assert.h>

#define ZXDEBUG 0

namespace fs {
namespace {

void WriteDescribeError(zx::channel channel, zx_status_t status) {
    zxrio_describe_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.op = ZXFIDL_ON_OPEN;
    msg.status = status;
    channel.write(0, &msg, sizeof(zxrio_describe_t), nullptr, 0);
}

void Describe(const fbl::RefPtr<Vnode>& vn, uint32_t flags,
              zxrio_describe_t* response, zx_handle_t* handle) {
    response->op = ZXFIDL_ON_OPEN;
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

void FilterFlags(uint32_t flags, uint32_t* out_flags, bool* out_describe) {
    // Filter out flags that are invalid when combined with REF_ONLY.
    if (IsPathOnly(flags)) {
        flags &= ZX_FS_FLAG_VNODE_REF_ONLY | ZX_FS_FLAG_DIRECTORY | ZX_FS_FLAG_DESCRIBE;
    }

    *out_describe = flags & ZX_FS_FLAG_DESCRIBE;
    *out_flags = flags & (~ZX_FS_FLAG_DESCRIBE);
}

void VnodeServe(Vfs* vfs, fbl::RefPtr<Vnode> vnode, zx::channel channel, uint32_t open_flags) {
    if (IsPathOnly(open_flags)) {
        vnode->Vnode::Serve(vfs, fbl::move(channel), open_flags);
    } else {
        vnode->Serve(vfs, fbl::move(channel), open_flags);
    }
}

// Performs a path walk and opens a connection to another node.
void OpenAt(Vfs* vfs, fbl::RefPtr<Vnode> parent, zx::channel channel,
            fbl::StringPiece path, uint32_t flags, uint32_t mode) {
    bool describe;
    uint32_t open_flags;
    FilterFlags(flags, &open_flags, &describe);

    fbl::RefPtr<Vnode> vnode;
    zx_status_t r = vfs->Open(fbl::move(parent), &vnode, path, &path, open_flags, mode);

    if (r != ZX_OK) {
        xprintf("vfs: open: r=%d\n", r);
    } else if (!(open_flags & ZX_FS_FLAG_NOREMOTE) && vnode->IsRemote()) {
        // Remote handoff to a remote filesystem node.
        char bytes[ZXFIDL_MAX_MSG_BYTES];
        fuchsia_io_DirectoryOpenRequest* request =
            reinterpret_cast<fuchsia_io_DirectoryOpenRequest*>(&bytes);
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
        zx_handle_t handle = channel.release();
        uint32_t num_bytes =
                static_cast<uint32_t>(FIDL_ALIGN(sizeof(fuchsia_io_DirectoryOpenRequest)))
                + static_cast<uint32_t>(FIDL_ALIGN(path.length()));
        fidl_msg_t msg = {
            .bytes = bytes,
            .handles = &handle,
            .num_bytes = num_bytes,
            .num_handles = 1u,
        };
        vfs->ForwardMessageRemote(fbl::move(vnode), &msg);
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

    VnodeServe(vfs, fbl::move(vnode), fbl::move(channel), open_flags);
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
    return wait_.Begin(vfs_->dispatcher());
}

void Connection::HandleSignals(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
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
                status = wait_.Begin(dispatcher);
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

zx_status_t Connection::HandleMessageThunk(fidl_msg_t* msg, void* cookie) {
    Connection* connection = static_cast<Connection*>(cookie);
    return connection->HandleMessage(msg);
}

// Flags which can be modified by SetFlags
constexpr uint32_t kStatusFlags = ZX_FS_FLAG_APPEND;

zx_status_t Connection::HandleMessage(fidl_msg_t* msg) {
    fidl_message_header_t* hdr = reinterpret_cast<fidl_message_header_t*>(msg->bytes);
    switch (hdr->ordinal) {
    case ZXFIDL_OPEN: {
        TRACE_DURATION("vfs", "ZXFIDL_OPEN");
        auto request = reinterpret_cast<fuchsia_io_DirectoryOpenRequest*>(hdr);

        uint32_t flags = request->flags;
        uint32_t mode = request->mode;
        char* path = request->path.data;
        uint32_t len = static_cast<uint32_t>(request->path.size);
        zx::channel channel(request->object);
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
    case ZXFIDL_CLOSE: {
        TRACE_DURATION("vfs", "ZXFIDL_CLOSE");
        if (!IsPathOnly(flags_)) {
            return vnode_->Close();
        }
        return ZX_OK;
    }
    case ZXFIDL_CLONE: {
        TRACE_DURATION("vfs", "ZXFIDL_CLONE");
        auto request = reinterpret_cast<fuchsia_io_ObjectCloneRequest*>(hdr);

        zx::channel channel;
        uint32_t flags;

        channel.reset(request->object);
        flags = request->flags;

        bool describe;
        uint32_t open_flags;
        FilterFlags(flags, &open_flags, &describe);
        // TODO(smklein): Avoid automatically inheriting rights
        // from the cloned file descriptor; allow de-scoping.
        // Currently, this is difficult, since the remote IO interface
        // to clone does not specify a reduced set of rights.
        open_flags |= (flags_ & ZX_FS_RIGHTS);

        fbl::RefPtr<Vnode> vn(vnode_);
        zx_status_t status = ZX_OK;
        if (!IsPathOnly(open_flags)) {
            status = OpenVnode(open_flags, &vn);
        }
        if (describe) {
            zxrio_describe_t response;
            memset(&response, 0, sizeof(response));
            response.status = status;
            zx_handle_t extra = ZX_HANDLE_INVALID;
            if (status == ZX_OK) {
                Describe(vnode_, open_flags, &response, &extra);
            }
            uint32_t hcount = (extra != ZX_HANDLE_INVALID) ? 1 : 0;
            channel.write(0, &response, sizeof(zxrio_describe_t), &extra, hcount);
        }

        if (status == ZX_OK) {
            VnodeServe(vfs_, fbl::move(vn), fbl::move(channel), open_flags);
        }
        return ERR_DISPATCHER_INDIRECT;
    }
    case ZXFIDL_READ: {
        TRACE_DURATION("vfs", "ZXFIDL_READ");
        if (!IsReadable(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }
        auto request = reinterpret_cast<fuchsia_io_FileReadRequest*>(hdr);
        auto response = reinterpret_cast<fuchsia_io_FileReadResponse*>(hdr);
        void* data;
        data = (void*)((uintptr_t)response + FIDL_ALIGN(sizeof(fuchsia_io_FileReadResponse)));
        uint32_t len = static_cast<uint32_t>(request->count);
        size_t actual;
        zx_status_t status = vnode_->Read(data, len, offset_, &actual);
        if (status == ZX_OK) {
            ZX_DEBUG_ASSERT(actual <= static_cast<size_t>(len));
            offset_ += actual;
            response->data.count = actual;
        }
        return status;
    }
    case ZXFIDL_READ_AT: {
        TRACE_DURATION("vfs", "ZXFIDL_READ_AT");
        if (!IsReadable(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }
        auto request = reinterpret_cast<fuchsia_io_FileReadAtRequest*>(hdr);
        auto response = reinterpret_cast<fuchsia_io_FileReadAtResponse*>(hdr);
        void* data = (void*)((uintptr_t)response +
                             FIDL_ALIGN(sizeof(fuchsia_io_FileReadAtResponse)));
        uint32_t len = static_cast<uint32_t>(request->count);
        uint64_t offset = request->offset;
        size_t actual;
        zx_status_t status = vnode_->Read(data, len, offset, &actual);
        if (status == ZX_OK) {
            ZX_DEBUG_ASSERT(actual <= static_cast<size_t>(len));
            response->data.count = actual;
        }
        return status;
    }
    case ZXFIDL_WRITE: {
        TRACE_DURATION("vfs", "ZXFIDL_WRITE");
        fuchsia_io_FileWriteRequest* request =
                reinterpret_cast<fuchsia_io_FileWriteRequest*>(hdr);
        fuchsia_io_FileWriteResponse* response =
                reinterpret_cast<fuchsia_io_FileWriteResponse*>(hdr);
        void* data = request->data.data;
        uint32_t len = static_cast<uint32_t>(request->data.count);

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
        response->actual = actual;
        return status;
    }
    case ZXFIDL_WRITE_AT: {
        TRACE_DURATION("vfs", "ZXFIDL_WRITE_AT");
        fuchsia_io_FileWriteAtRequest* request =
            reinterpret_cast<fuchsia_io_FileWriteAtRequest*>(hdr);
        fuchsia_io_FileWriteAtResponse* response =
            reinterpret_cast<fuchsia_io_FileWriteAtResponse*>(hdr);
        void* data = request->data.data;
        uint32_t len = static_cast<uint32_t>(request->data.count);
        uint64_t offset = request->offset;
        if (!IsWritable(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }
        size_t actual = 0;
        zx_status_t status = vnode_->Write(data, len, offset, &actual);
        ZX_DEBUG_ASSERT(actual <= static_cast<size_t>(len));
        response->actual = actual;
        return status;
    }
    case ZXFIDL_SEEK: {
        TRACE_DURATION("vfs", "ZXFIDL_SEEK");
        fuchsia_io_FileSeekRequest* request = reinterpret_cast<fuchsia_io_FileSeekRequest*>(hdr);
        fuchsia_io_FileSeekResponse* response = reinterpret_cast<fuchsia_io_FileSeekResponse*>(hdr);

        static_assert(SEEK_SET == fuchsia_io_SeekOrigin_Start, "");
        static_assert(SEEK_CUR == fuchsia_io_SeekOrigin_Current, "");
        static_assert(SEEK_END == fuchsia_io_SeekOrigin_End, "");
        off_t offset = request->offset;
        int whence = request->start;

        if (IsPathOnly(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }
        vnattr_t attr;
        zx_status_t r;
        if ((r = vnode_->Getattr(&attr)) < 0) {
            return r;
        }
        size_t n;
        switch (whence) {
        case SEEK_SET:
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
        response->offset = offset_;
        return ZX_OK;
    }
    case ZXFIDL_STAT: {
        TRACE_DURATION("vfs", "ZXFIDL_STAT");
        auto response = reinterpret_cast<fuchsia_io_NodeGetAttrResponse*>(hdr);

        // TODO(smklein): Consider using "NodeAttributes" within
        // ulib/fs, rather than vnattr_t.
        // Alternatively modify vnattr_t to match "NodeAttributes"
        vnattr_t attr;
        zx_status_t r;
        if ((r = vnode_->Getattr(&attr)) != ZX_OK) {
            return r;
        }

        response->attributes.mode = attr.mode;
        response->attributes.id = attr.inode;
        response->attributes.content_size = attr.size;
        response->attributes.storage_size = VNATTR_BLKSIZE * attr.blkcount;
        response->attributes.link_count = attr.nlink;
        response->attributes.creation_time = attr.create_time;
        response->attributes.modification_time = attr.modify_time;
        return r;
    }
    case ZXFIDL_SETATTR: {
        TRACE_DURATION("vfs", "ZXFIDL_SETATTR");
        auto request = reinterpret_cast<fuchsia_io_NodeSetAttrRequest*>(hdr);

        // TODO(smklein): Prevent read-only files from setting attributes,
        // but allow attribute-setting on mutable directories.
        // For context: ZX-1262, ZX-1065
        if (IsPathOnly(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }

        vnattr_t attr;
        attr.valid = request->flags;
        attr.create_time = request->attributes.creation_time;
        attr.modify_time = request->attributes.modification_time;
        return vnode_->Setattr(&attr);
    }
    case ZXFIDL_GET_FLAGS: {
        TRACE_DURATION("vfs", "ZXFIDL_GET_FLAGS");
        fuchsia_io_FileGetFlagsResponse* response =
            reinterpret_cast<fuchsia_io_FileGetFlagsResponse*>(hdr);
        response->flags = flags_ & (kStatusFlags | ZX_FS_RIGHTS | ZX_FS_FLAG_VNODE_REF_ONLY);
        return ZX_OK;
    }
    case ZXFIDL_SET_FLAGS: {
        TRACE_DURATION("vfs", "ZXFIDL_SET_FLAGS");
        fuchsia_io_FileSetFlagsRequest* request =
            reinterpret_cast<fuchsia_io_FileSetFlagsRequest*>(hdr);
        flags_ = (flags_ & ~kStatusFlags) | (request->flags & kStatusFlags);
        return ZX_OK;
    }
    case ZXFIDL_REWIND: {
        TRACE_DURATION("vfs", "ZXFIDL_REWIND");
        if (IsPathOnly(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }
        dircookie_.Reset();
        return ZX_OK;
    }
    case ZXFIDL_READDIR: {
        TRACE_DURATION("vfs", "ZXFIDL_READDIR");
        if (IsPathOnly(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }

        auto request = reinterpret_cast<fuchsia_io_DirectoryReadDirentsRequest*>(hdr);
        auto response = reinterpret_cast<fuchsia_io_DirectoryReadDirentsResponse*>(hdr);
        void* data = (void*)((uintptr_t)response +
                    FIDL_ALIGN(sizeof(fuchsia_io_DirectoryReadDirentsResponse)));
        uint32_t max_out = static_cast<uint32_t>(request->max_out);

        if (max_out > FDIO_CHUNK_SIZE) {
            return ZX_ERR_INVALID_ARGS;
        }
        size_t actual;
        zx_status_t r = vfs_->Readdir(vnode_.get(), &dircookie_, data, max_out, &actual);
        if (r == ZX_OK) {
            response->dirents.count = actual;
        }
        return r;
    }
    case ZXFIDL_IOCTL: {
        auto request = reinterpret_cast<fuchsia_io_NodeIoctlRequest*>(hdr);
        auto response = reinterpret_cast<fuchsia_io_NodeIoctlResponse*>(hdr);

        void* secondary = (void*)((uintptr_t)(hdr) +
                FIDL_ALIGN(sizeof(fuchsia_io_NodeIoctlResponse)));
        uint32_t op = request->opcode;
        zx_handle_t* handles = static_cast<zx_handle_t*>(request->handles.data);
        size_t hcount = request->handles.count;
        void* in = request->in.data;
        size_t inlen = request->in.count;
        void* out = secondary;
        size_t outlen = request->max_out;

        zx::handle handle;
        if (hcount == 1) {
            handle.reset(handles[0]);
            if (IOCTL_KIND(op) != IOCTL_KIND_SET_HANDLE) {
                return ZX_ERR_INVALID_ARGS;
            }
            if (inlen < sizeof(zx_handle_t)) {
                inlen = sizeof(zx_handle_t);
            }
        } else if (hcount > 1) {
            zx_handle_close_many(handles, hcount);
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
            zx_txid_t txid = hdr->txid;

            // "IOCTL_VFS_UNMOUNT_FS" is fatal to the requesting connections.
            Vfs::ShutdownCallback closure([ch = fbl::move(channel_), txid]
                                          (zx_status_t status) {
                fuchsia_io_NodeIoctlResponse rsp;
                memset(&rsp, 0, sizeof(rsp));
                rsp.hdr.txid = txid;
                rsp.hdr.ordinal = ZXFIDL_IOCTL;
                fidl_msg_t msg = {
                    .bytes = &rsp,
                    .handles = nullptr,
                    .num_bytes = sizeof(rsp),
                    .num_handles = 0u,
                };
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
        response->handles.count = hcount;
        response->handles.data = secondary;
        response->out.count = actual;
        response->out.data = secondary;
        return ZX_OK;
    }
    case ZXFIDL_TRUNCATE: {
        TRACE_DURATION("vfs", "ZXFIDL_TRUNCATE");
        if (!IsWritable(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }

        auto request = reinterpret_cast<fuchsia_io_FileTruncateRequest*>(hdr);
        uint64_t length = request->length;
        return vnode_->Truncate(length);
    }
    case ZXFIDL_GET_TOKEN: {
        TRACE_DURATION("vfs", "ZXFIDL_GET_TOKEN");
        auto response = reinterpret_cast<fuchsia_io_DirectoryGetTokenResponse*>(hdr);
        zx::event returned_token;
        zx_status_t status = vfs_->VnodeToToken(vnode_, &token_, &returned_token);
        if (status == ZX_OK) {
            response->token = returned_token.release();
        }
        return status;
    }
    case ZXFIDL_RENAME: {
        TRACE_DURATION("vfs", "ZXFIDL_RENAME");
        auto request = reinterpret_cast<fuchsia_io_DirectoryRenameRequest*>(hdr);
        zx::event token(request->dst_parent_token);
        fbl::StringPiece oldStr(request->src.data, request->src.size);
        fbl::StringPiece newStr(request->dst.data, request->dst.size);

        if (request->src.size < 1 || request->dst.size < 1) {
            return ZX_ERR_INVALID_ARGS;
        }
        return vfs_->Rename(fbl::move(token), vnode_, fbl::move(oldStr), fbl::move(newStr));
    }
    case ZXFIDL_LINK: {
        TRACE_DURATION("vfs", "ZXFIDL_LINK");
        auto request = reinterpret_cast<fuchsia_io_DirectoryLinkRequest*>(hdr);
        zx::event token(request->dst_parent_token);
        fbl::StringPiece oldStr(request->src.data, request->src.size);
        fbl::StringPiece newStr(request->dst.data, request->dst.size);

        if (request->src.size < 1 || request->dst.size < 1) {
            return ZX_ERR_INVALID_ARGS;
        }
        return vfs_->Link(fbl::move(token), vnode_, fbl::move(oldStr), fbl::move(newStr));
    }
    case ZXFIDL_GET_VMO: {
        TRACE_DURATION("vfs", "ZXFIDL_GET_VMO");
        if (IsPathOnly(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }

        auto request = reinterpret_cast<fuchsia_io_FileGetVmoRequest*>(hdr);
        auto response = reinterpret_cast<fuchsia_io_FileGetVmoResponse*>(hdr);
        uint32_t flags = request->flags;
        zx_handle_t* handle = &response->vmo;

        if ((flags & FDIO_MMAP_FLAG_PRIVATE) && (flags & FDIO_MMAP_FLAG_EXACT)) {
            return ZX_ERR_INVALID_ARGS;
        } else if ((flags_ & ZX_FS_FLAG_APPEND) && flags & FDIO_MMAP_FLAG_WRITE) {
            return ZX_ERR_ACCESS_DENIED;
        } else if (!IsWritable(flags_) && (flags & FDIO_MMAP_FLAG_WRITE)) {
            return ZX_ERR_ACCESS_DENIED;
        } else if (!IsReadable(flags_)) {
            return ZX_ERR_ACCESS_DENIED;
        }

        return vnode_->GetVmo(flags, handle);
    }
    case ZXFIDL_SYNC: {
        TRACE_DURATION("vfs", "ZXFIDL_SYNC");
        if (IsPathOnly(flags_)) {
            return ZX_ERR_BAD_HANDLE;
        }
        zx_txid_t txid = hdr->txid;
        Vnode::SyncCallback closure([this, txid] (zx_status_t status) {
            fuchsia_io_NodeSyncResponse rsp;
            memset(&rsp, 0, sizeof(rsp));
            rsp.hdr.txid = txid;
            rsp.hdr.ordinal = ZXFIDL_SYNC;
            fidl_msg_t msg = {
                .bytes = &rsp,
                .handles = nullptr,
                .num_bytes = 0u,
                .num_handles = 0u,
            };
            zxrio_write_response(channel_.get(), status, &msg);

            // Try to reset the wait object
            ZX_ASSERT_MSG(wait_.Begin(vfs_->dispatcher()) == ZX_OK,
                          "Dispatch loop unexpectedly ended");
        });

        vnode_->Sync(fbl::move(closure));
        return ERR_DISPATCHER_ASYNC;
    }
    case ZXFIDL_UNLINK: {
        TRACE_DURATION("vfs", "ZXFIDL_UNLINK");
        fuchsia_io_DirectoryUnlinkRequest* request =
            reinterpret_cast<fuchsia_io_DirectoryUnlinkRequest*>(hdr);
        char* data = request->path.data;
        uint32_t datalen = static_cast<uint32_t>(request->path.size);
        return vfs_->Unlink(vnode_, fbl::StringPiece(data, datalen));
    }
    default:
        fprintf(stderr, "connection.cpp: Unsupported FIDL operation: 0x%x\n", hdr->ordinal);
        zx_handle_close_many(msg->handles, msg->num_handles);
        return ZX_ERR_NOT_SUPPORTED;
    }
}

} // namespace fs
