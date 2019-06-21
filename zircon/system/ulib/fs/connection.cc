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

#include <fbl/string_buffer.h>
#include <fs/handler.h>
#include <fs/trace.h>
#include <fs/vnode.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/io.h>
#include <lib/fdio/vfs.h>
#include <lib/zircon-internal/debug.h>
#include <lib/zx/handle.h>
#include <zircon/assert.h>

#include <memory>
#include <utility>

#include "debug.h"

#ifdef __Fuchsia__
static_assert(fuchsia_io_OPEN_FLAGS_ALLOWED_WITH_NODE_REFERENCE == (fuchsia_io_OPEN_FLAG_DIRECTORY |
                                                                    fuchsia_io_OPEN_FLAG_NOT_DIRECTORY |
                                                                    fuchsia_io_OPEN_FLAG_DESCRIBE |
                                                                    fuchsia_io_OPEN_FLAG_NODE_REFERENCE),
              "OPEN_FLAGS_ALLOWED_WITH_NODE_REFERENCE value mismatch");
static_assert(PATH_MAX == fuchsia_io_MAX_PATH,
              "POSIX PATH_MAX inconsistent with Fuchsia MAX_PATH");
static_assert(NAME_MAX == fuchsia_io_MAX_FILENAME,
              "POSIX NAME_MAX inconsistent with Fuchsia MAX_FILENAME");
#endif

namespace fs {

namespace {

// Returns true if the rights flags in |flags_a| does not exceed those in |flags_b|.
bool StricterOrSameRights(uint32_t flags_a, uint32_t flags_b) {
    uint32_t rights_a = flags_a & ZX_FS_RIGHTS;
    uint32_t rights_b = flags_b & ZX_FS_RIGHTS;
    return (rights_a & ~rights_b) == 0;
}

void WriteDescribeError(zx::channel channel, zx_status_t status) {
    fuchsia_io_NodeOnOpenEvent msg;
    memset(&msg, 0, sizeof(msg));
    msg.hdr.ordinal = fuchsia_io_NodeOnOpenOrdinal;
    msg.s = status;
    channel.write(0, &msg, sizeof(msg), nullptr, 0);
}

zx_status_t GetNodeInfo(const fbl::RefPtr<Vnode>& vn, uint32_t flags,
                        fuchsia_io_NodeInfo* info) {
    if (IsVnodeRefOnly(flags)) {
        info->tag = fuchsia_io_NodeInfoTag_service;
        return ZX_OK;
    } else {
        return vn->GetNodeInfo(flags, info);
    }
}

void Describe(const fbl::RefPtr<Vnode>& vn, uint32_t flags,
              OnOpenMsg* response, zx_handle_t* handle) {
    response->primary.hdr.ordinal = fuchsia_io_NodeOnOpenOrdinal;
    response->extra.file.event = ZX_HANDLE_INVALID;
    zx_status_t r = GetNodeInfo(vn, flags, &response->extra);

    // We unfortunately encode this message by hand because FIDL events
    // are not yet supported by the C bindings.
    auto encode_handle = [](zx_handle_t* encode_location, zx_handle_t* out) {
        // If a handle was returned, transfer it to the output location, and
        // encode it in-place.
        *out = *encode_location;
        if (*encode_location != ZX_HANDLE_INVALID) {
            *encode_location = FIDL_HANDLE_PRESENT;
        } else {
            *encode_location = FIDL_HANDLE_ABSENT;
        }
    };
    switch (response->extra.tag) {
    case fuchsia_io_NodeInfoTag_service:
    case fuchsia_io_NodeInfoTag_directory:
        break;
    case fuchsia_io_NodeInfoTag_file:
        encode_handle(&response->extra.file.event, handle);
        break;
    case fuchsia_io_NodeInfoTag_pipe:
        encode_handle(&response->extra.pipe.socket, handle);
        break;
    case fuchsia_io_NodeInfoTag_vmofile:
        encode_handle(&response->extra.vmofile.vmo, handle);
        break;
    case fuchsia_io_NodeInfoTag_device:
        encode_handle(&response->extra.device.event, handle);
        break;
    case fuchsia_io_NodeInfoTag_tty:
        encode_handle(&response->extra.tty.event, handle);
        break;
    default:
        ZX_DEBUG_ASSERT_MSG(false, "Unsupported NodeInfoTag: %d\n", response->extra.tag);
    }

    // If a valid response was returned, encode it.
    response->primary.s = r;
    response->primary.info = reinterpret_cast<fuchsia_io_NodeInfo*>(r == ZX_OK ? FIDL_ALLOC_PRESENT : FIDL_ALLOC_ABSENT);
}

// Perform basic flags sanitization and extract |ZX_FS_FLAG_DESCRIBE| bit into bool |out_describe|.
// Returns false if the flags combination is invalid.
bool PrevalidateFlags(uint32_t flags, uint32_t* out_flags, bool* out_describe) {
    *out_describe = flags & ZX_FS_FLAG_DESCRIBE;

    // If the caller specified an unknown right, reject the request.
    if ((flags & ZX_FS_RIGHTS_SPACE) & ~ZX_FS_RIGHTS) {
        return false;
    }

    if (IsVnodeRefOnly(flags)) {
        constexpr uint32_t kValidFlagsForNodeRef =
            ZX_FS_FLAG_VNODE_REF_ONLY | ZX_FS_FLAG_DIRECTORY | ZX_FS_FLAG_NOT_DIRECTORY |
            ZX_FS_FLAG_DESCRIBE;
        // Explicitly reject VNODE_REF_ONLY together with any invalid flags.
        if (flags & ~kValidFlagsForNodeRef) {
            return false;
        }
    }

    *out_flags = flags & (~ZX_FS_FLAG_DESCRIBE);
    return true;
}

zx_status_t EnforceHierarchicalRights(uint32_t parent_flags, uint32_t child_flags,
                                      uint32_t* out_flags) {
    if (child_flags & ZX_FS_FLAG_POSIX) {
        if (!IsWritable(parent_flags) && !IsWritable(child_flags)) {
            // Posix compatibility flag allows the child dir connection to inherit every right from
            // its immediate parent. Here we know there exists a read-only directory somewhere along
            // the Open() chain, so remove this flag to rid the child connection the ability to
            // inherit read-write right from e.g. crossing a read-write mount point down the line.
            child_flags &= ~ZX_FS_FLAG_POSIX;
        }
    }
    if (!StricterOrSameRights(child_flags, parent_flags)) {
        // Client asked for some right but we do not have it
        return ZX_ERR_ACCESS_DENIED;
    }
    *out_flags = child_flags;
    return ZX_OK;
}

void VnodeServe(Vfs* vfs, const fbl::RefPtr<Vnode>& vnode, zx::channel channel,
                uint32_t open_flags) {
    if (IsVnodeRefOnly(open_flags)) {
        vnode->Vnode::Serve(vfs, std::move(channel), open_flags);
    } else {
        vnode->Serve(vfs, std::move(channel), open_flags);
    }
}

// Performs a path walk and opens a connection to another node.
void OpenAt(Vfs* vfs, fbl::RefPtr<Vnode> parent, zx::channel channel,
            fbl::StringPiece path, uint32_t flags, uint32_t parent_rights, uint32_t mode) {
    bool describe;
    uint32_t open_flags;
    if (!PrevalidateFlags(flags, &open_flags, &describe)) {
        if (describe) {
            WriteDescribeError(std::move(channel), ZX_ERR_INVALID_ARGS);
        }
        return;
    }

    fbl::RefPtr<Vnode> vnode;
    zx_status_t r = vfs->Open(std::move(parent), &vnode, path, &path, open_flags, mode);

    if (r != ZX_OK) {
        FS_TRACE_DEBUG("vfs: open failure: %d\n", r);
    } else if (!(open_flags & ZX_FS_FLAG_NOREMOTE) && vnode->IsRemote()) {
        FS_TRACE_DEBUG("vfs: handoff to remote\n");
        // Remote handoff to a remote filesystem node.
        vfs->ForwardOpenRemote(std::move(vnode), std::move(channel), path, flags, mode);
        return;
    }

    if (describe) {
        // Regardless of the error code, in the 'describe' case, we
        // should respond to the client.
        if (r != ZX_OK) {
            WriteDescribeError(std::move(channel), r);
            return;
        }

        OnOpenMsg response;
        memset(&response, 0, sizeof(response));
        zx_handle_t extra = ZX_HANDLE_INVALID;
        Describe(vnode, flags, &response, &extra);
        uint32_t hcount = (extra != ZX_HANDLE_INVALID) ? 1 : 0;
        channel.write(0, &response, sizeof(OnOpenMsg), &extra, hcount);
    } else if (r != ZX_OK) {
        return;
    }

    if (vnode->IsDirectory() && (open_flags & ZX_FS_FLAG_POSIX)) {
        // This is such that POSIX open() can open a directory with O_RDONLY, and still get a
        // RW directory if the parent directory is RW.
        // ADMIN right is not inherited. It must be explicitly specified.
        bool admin = open_flags & ZX_FS_RIGHT_ADMIN;
        open_flags |= parent_rights;
        open_flags &= ~ZX_FS_RIGHT_ADMIN;
        if (admin) {
            open_flags |= ZX_FS_RIGHT_ADMIN;
        }
    }

    VnodeServe(vfs, vnode, std::move(channel), open_flags);
}

// This template defines a mechanism to transform a member of Connection
// into a FIDL-dispatch operation compatible format, independent of
// FIDL arguments.
//
// For example:
//
//      ZXFIDL_OPERATION(Foo)
//
// Defines the following method:
//
//      zx_status_t FooOp(void* ctx, Args... args);
//
// That invokes:
//
//      zx_status_t Connection::Foo(Args... args);
//
// Such that FooOp may be used in the fuchsia_io_* ops table.
#define ZXFIDL_OPERATION(Method)                                              \
    template <typename... Args>                                               \
    zx_status_t Method##Op(void* ctx, Args... args) {                         \
        TRACE_DURATION("vfs", #Method);                                       \
        auto connection = reinterpret_cast<Connection*>(ctx);                 \
        return (connection->Connection::Method)(std::forward<Args>(args)...); \
    }

ZXFIDL_OPERATION(NodeClone)
ZXFIDL_OPERATION(NodeClose)
ZXFIDL_OPERATION(NodeDescribe)
ZXFIDL_OPERATION(NodeSync)
ZXFIDL_OPERATION(NodeGetAttr)
ZXFIDL_OPERATION(NodeSetAttr)
ZXFIDL_OPERATION(NodeIoctl)

const fuchsia_io_Node_ops kNodeOps = {
    .Clone = NodeCloneOp,
    .Close = NodeCloseOp,
    .Describe = NodeDescribeOp,
    .Sync = NodeSyncOp,
    .GetAttr = NodeGetAttrOp,
    .SetAttr = NodeSetAttrOp,
    .Ioctl = NodeIoctlOp,
};

ZXFIDL_OPERATION(FileRead)
ZXFIDL_OPERATION(FileReadAt)
ZXFIDL_OPERATION(FileWrite)
ZXFIDL_OPERATION(FileWriteAt)
ZXFIDL_OPERATION(FileSeek)
ZXFIDL_OPERATION(FileTruncate)
ZXFIDL_OPERATION(FileGetFlags)
ZXFIDL_OPERATION(FileSetFlags)
ZXFIDL_OPERATION(FileGetBuffer)

const fuchsia_io_File_ops kFileOps = {
    .Clone = NodeCloneOp,
    .Close = NodeCloseOp,
    .Describe = NodeDescribeOp,
    .Sync = NodeSyncOp,
    .GetAttr = NodeGetAttrOp,
    .SetAttr = NodeSetAttrOp,
    .Ioctl = NodeIoctlOp,
    .Read = FileReadOp,
    .ReadAt = FileReadAtOp,
    .Write = FileWriteOp,
    .WriteAt = FileWriteAtOp,
    .Seek = FileSeekOp,
    .Truncate = FileTruncateOp,
    .GetFlags = FileGetFlagsOp,
    .SetFlags = FileSetFlagsOp,
    .GetBuffer = FileGetBufferOp,
};

ZXFIDL_OPERATION(DirectoryOpen)
ZXFIDL_OPERATION(DirectoryUnlink)
ZXFIDL_OPERATION(DirectoryReadDirents)
ZXFIDL_OPERATION(DirectoryRewind)
ZXFIDL_OPERATION(DirectoryGetToken)
ZXFIDL_OPERATION(DirectoryRename)
ZXFIDL_OPERATION(DirectoryLink)
ZXFIDL_OPERATION(DirectoryWatch)

const fuchsia_io_Directory_ops kDirectoryOps{
    .Clone = NodeCloneOp,
    .Close = NodeCloseOp,
    .Describe = NodeDescribeOp,
    .Sync = NodeSyncOp,
    .GetAttr = NodeGetAttrOp,
    .SetAttr = NodeSetAttrOp,
    .Ioctl = NodeIoctlOp,
    .Open = DirectoryOpenOp,
    .Unlink = DirectoryUnlinkOp,
    .ReadDirents = DirectoryReadDirentsOp,
    .Rewind = DirectoryRewindOp,
    .GetToken = DirectoryGetTokenOp,
    .Rename = DirectoryRenameOp,
    .Link = DirectoryLinkOp,
    .Watch = DirectoryWatchOp,
};

ZXFIDL_OPERATION(DirectoryAdminMount)
ZXFIDL_OPERATION(DirectoryAdminMountAndCreate)
ZXFIDL_OPERATION(DirectoryAdminUnmount)
ZXFIDL_OPERATION(DirectoryAdminUnmountNode)
ZXFIDL_OPERATION(DirectoryAdminQueryFilesystem)
ZXFIDL_OPERATION(DirectoryAdminGetDevicePath)

const fuchsia_io_DirectoryAdmin_ops kDirectoryAdminOps{
    .Clone = NodeCloneOp,
    .Close = NodeCloseOp,
    .Describe = NodeDescribeOp,
    .Sync = NodeSyncOp,
    .GetAttr = NodeGetAttrOp,
    .SetAttr = NodeSetAttrOp,
    .Ioctl = NodeIoctlOp,
    .Open = DirectoryOpenOp,
    .Unlink = DirectoryUnlinkOp,
    .ReadDirents = DirectoryReadDirentsOp,
    .Rewind = DirectoryRewindOp,
    .GetToken = DirectoryGetTokenOp,
    .Rename = DirectoryRenameOp,
    .Link = DirectoryLinkOp,
    .Watch = DirectoryWatchOp,
    .Mount = DirectoryAdminMountOp,
    .MountAndCreate = DirectoryAdminMountAndCreateOp,
    .Unmount = DirectoryAdminUnmountOp,
    .UnmountNode = DirectoryAdminUnmountNodeOp,
    .QueryFilesystem = DirectoryAdminQueryFilesystemOp,
    .GetDevicePath = DirectoryAdminGetDevicePathOp,
};

} // namespace

constexpr zx_signals_t kWakeSignals = ZX_CHANNEL_READABLE |
                                      ZX_CHANNEL_PEER_CLOSED | kLocalTeardownSignal;

// Flags which can be modified by SetFlags.
constexpr uint32_t kSettableStatusFlags = ZX_FS_FLAG_APPEND;

// All flags which indicate state of the connection (excluding rights).
constexpr uint32_t kStatusFlags = kSettableStatusFlags | ZX_FS_FLAG_VNODE_REF_ONLY;

// Flags which should be stored on the connection object.
// Some flags (e.g. ZX_FS_FLAG_POSIX) only affect the interpretation of rights at the time of Open,
// and should have no effects thereafter. Hence we filter them here.
// TODO(ZX-3543): Some of these flag groups should be defined in io.fidl and use that as the source
// of truth.
constexpr uint32_t kPersistentFlags = ZX_FS_RIGHTS | kStatusFlags;

Connection::Connection(Vfs* vfs, fbl::RefPtr<Vnode> vnode,
                       zx::channel channel, uint32_t flags)
    : vfs_(vfs), vnode_(std::move(vnode)), channel_(std::move(channel)),
      wait_(this, ZX_HANDLE_INVALID, kWakeSignals),
      flags_(flags & kPersistentFlags) {
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
        vfs_->TokenDiscard(std::move(token_));
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

void Connection::HandleSignals(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                               zx_status_t status, const zx_packet_signal_t* signal) {
    ZX_DEBUG_ASSERT(is_open());

    if (status == ZX_OK) {
        if (vfs_->IsTerminating()) {
            // Short-circuit locally destroyed connections, rather than servicing
            // requests on their behalf. This prevents new requests from being
            // opened while filesystems are torn down.
            status = ZX_ERR_PEER_CLOSED;
        } else if (signal->observed & ZX_CHANNEL_READABLE) {
            // Handle the message.
            status = ReadMessage(channel_.get(), [this](fidl_msg_t* msg, FidlConnection* txn) {
                return HandleMessage(msg, txn->Txn());
            });
            switch (status) {
            case ERR_DISPATCHER_ASYNC:
                return;
            case ZX_OK:
                status = wait_.Begin(dispatcher);
                if (status == ZX_OK) {
                    return;
                }
                break;
            default:
                // In case of error, go to |Terminate|.
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

void Connection::CallClose() {
    CloseMessage([this](fidl_msg_t* msg, FidlConnection* txn) {
        return HandleMessage(msg, txn->Txn());
    });
    set_closed();
}

zx_status_t Connection::NodeClone(uint32_t flags, zx_handle_t object) {
    FS_PRETTY_TRACE_DEBUG("[NodeClone] our flags: ", ZxFlags(flags_),
                          ", incoming flags: ", ZxFlags(flags));
    zx::channel channel(object);
    auto write_error = [describe = flags & ZX_FS_FLAG_DESCRIBE](zx::channel channel,
                                                                zx_status_t error) {
        if (describe) {
            WriteDescribeError(std::move(channel), error);
        }
        return ZX_OK;
    };

    bool describe;
    uint32_t clone_flags;
    if (!PrevalidateFlags(flags, &clone_flags, &describe)) {
        return write_error(std::move(channel), ZX_ERR_INVALID_ARGS);
    }
    // If CLONE_SAME_RIGHTS is specified, the client cannot request any specific rights.
    if ((clone_flags & ZX_FS_FLAG_CLONE_SAME_RIGHTS) && (clone_flags & ZX_FS_RIGHTS)) {
        return write_error(std::move(channel), ZX_ERR_INVALID_ARGS);
    }
    clone_flags |= (flags_ & kStatusFlags);
    // If CLONE_SAME_RIGHTS is requested, cloned connection will inherit the same rights
    // as those from the originating connection.
    if (clone_flags & ZX_FS_FLAG_CLONE_SAME_RIGHTS) {
        clone_flags &= (~ZX_FS_RIGHTS);
        clone_flags |= (flags_ & ZX_FS_RIGHTS);
    }
    if (!StricterOrSameRights(clone_flags, flags_)) {
        FS_PRETTY_TRACE_DEBUG("Rights violation during NodeClone");
        return write_error(std::move(channel), ZX_ERR_ACCESS_DENIED);
    }

    fbl::RefPtr<Vnode> vn(vnode_);
    zx_status_t status = ZX_OK;
    if (!IsVnodeRefOnly(clone_flags)) {
        status = OpenVnode(clone_flags, &vn);
    }
    if (describe) {
        OnOpenMsg response;
        memset(&response, 0, sizeof(response));
        response.primary.s = status;
        zx_handle_t extra = ZX_HANDLE_INVALID;
        if (status == ZX_OK) {
            Describe(vnode_, clone_flags, &response, &extra);
        }
        uint32_t hcount = (extra != ZX_HANDLE_INVALID) ? 1 : 0;
        channel.write(0, &response, sizeof(OnOpenMsg), &extra, hcount);
    }

    if (status == ZX_OK) {
        VnodeServe(vfs_, vn, std::move(channel), clone_flags);
    }
    return ZX_OK;
}

zx_status_t Connection::NodeClose(fidl_txn_t* txn) {
    zx_status_t status;
    if (IsVnodeRefOnly(flags_)) {
        status = ZX_OK;
    } else {
        status = vnode_->Close();
    }
    fuchsia_io_NodeClose_reply(txn, status);

    return ERR_DISPATCHER_DONE;
}

zx_status_t Connection::NodeDescribe(fidl_txn_t* txn) {
    fuchsia_io_NodeInfo info;
    memset(&info, 0, sizeof(info));
    zx_status_t status = GetNodeInfo(vnode_, flags_, &info);
    if (status != ZX_OK) {
        return status;
    }
    return fuchsia_io_NodeDescribe_reply(txn, &info);
}

zx_status_t Connection::NodeSync(fidl_txn_t* txn) {
    FS_PRETTY_TRACE_DEBUG("[NodeSync] flags: ", ZxFlags(flags_));

    if (IsVnodeRefOnly(flags_)) {
        return fuchsia_io_NodeSync_reply(txn, ZX_ERR_BAD_HANDLE);
    }
    Vnode::SyncCallback closure([this, ctxn = FidlConnection::CopyTxn(txn)](zx_status_t status) mutable {
        fuchsia_io_NodeSync_reply(ctxn.Txn(), status);

        // Try to reset the wait object
        ZX_ASSERT_MSG(wait_.Begin(vfs_->dispatcher()) == ZX_OK,
                      "Dispatch loop unexpectedly ended");
    });

    vnode_->Sync(std::move(closure));
    return ERR_DISPATCHER_ASYNC;
}

zx_status_t Connection::NodeGetAttr(fidl_txn_t* txn) {
    FS_PRETTY_TRACE_DEBUG("[NodeGetAttr] flags: ", ZxFlags(flags_));

    fuchsia_io_NodeAttributes attributes;
    memset(&attributes, 0, sizeof(attributes));

    // TODO(smklein): Consider using "NodeAttributes" within
    // ulib/fs, rather than vnattr_t.
    // Alternatively modify vnattr_t to match "NodeAttributes"
    vnattr_t attr;
    zx_status_t r;
    if ((r = vnode_->Getattr(&attr)) != ZX_OK) {
        return fuchsia_io_NodeGetAttr_reply(txn, r, &attributes);
    }

    attributes.mode = attr.mode;
    attributes.id = attr.inode;
    attributes.content_size = attr.size;
    attributes.storage_size = VNATTR_BLKSIZE * attr.blkcount;
    attributes.link_count = attr.nlink;
    attributes.creation_time = attr.create_time;
    attributes.modification_time = attr.modify_time;

    return fuchsia_io_NodeGetAttr_reply(txn, ZX_OK, &attributes);
}

zx_status_t Connection::NodeSetAttr(uint32_t flags,
                                    const fuchsia_io_NodeAttributes* attributes,
                                    fidl_txn_t* txn) {
    FS_PRETTY_TRACE_DEBUG("[NodeSetAttr] our flags: ", ZxFlags(flags_),
                          ", incoming flags: ", ZxFlags(flags));

    if (IsVnodeRefOnly(flags_)) {
        return fuchsia_io_NodeSetAttr_reply(txn, ZX_ERR_BAD_HANDLE);
    }
    if (!IsWritable(flags_)) {
        return fuchsia_io_NodeSetAttr_reply(txn, ZX_ERR_BAD_HANDLE);
    }

    vnattr_t attr;
    attr.valid = flags;
    attr.create_time = attributes->creation_time;
    attr.modify_time = attributes->modification_time;
    zx_status_t status = vnode_->Setattr(&attr);
    return fuchsia_io_NodeSetAttr_reply(txn, status);
}

zx_status_t Connection::NodeIoctl(uint32_t opcode, uint64_t max_out,
                                  const zx_handle_t* handles, size_t handles_count,
                                  const uint8_t* in_data, size_t in_count, fidl_txn_t* txn) {
    zx_handle_close_many(handles, handles_count);
    return fuchsia_io_NodeIoctl_reply(txn, ZX_ERR_NOT_SUPPORTED, nullptr, 0, nullptr, 0);
}

zx_status_t Connection::FileRead(uint64_t count, fidl_txn_t* txn) {
    FS_PRETTY_TRACE_DEBUG("[FileRead] flags: ", ZxFlags(flags_));

    if (IsVnodeRefOnly(flags_)) {
        return fuchsia_io_FileRead_reply(txn, ZX_ERR_BAD_HANDLE, nullptr, 0);
    } else if (!IsReadable(flags_)) {
        return fuchsia_io_FileRead_reply(txn, ZX_ERR_BAD_HANDLE, nullptr, 0);
    } else if (count > ZXFIDL_MAX_MSG_BYTES) {
        return fuchsia_io_FileRead_reply(txn, ZX_ERR_INVALID_ARGS, nullptr, 0);
    }
    uint8_t data[count];
    size_t actual = 0;
    zx_status_t status = vnode_->Read(data, count, offset_, &actual);
    if (status == ZX_OK) {
        ZX_DEBUG_ASSERT(actual <= count);
        offset_ += actual;
    }
    return fuchsia_io_FileRead_reply(txn, status, data, actual);
}

zx_status_t Connection::FileReadAt(uint64_t count, uint64_t offset, fidl_txn_t* txn) {
    FS_PRETTY_TRACE_DEBUG("[FileReadAt] flags: ", ZxFlags(flags_));

    if (IsVnodeRefOnly(flags_)) {
        return fuchsia_io_FileReadAt_reply(txn, ZX_ERR_BAD_HANDLE, nullptr, 0);
    } else if (!IsReadable(flags_)) {
        return fuchsia_io_FileReadAt_reply(txn, ZX_ERR_BAD_HANDLE, nullptr, 0);
    } else if (count > ZXFIDL_MAX_MSG_BYTES) {
        return fuchsia_io_FileReadAt_reply(txn, ZX_ERR_INVALID_ARGS, nullptr, 0);
    }
    uint8_t data[count];
    size_t actual = 0;
    zx_status_t status = vnode_->Read(data, count, offset, &actual);
    if (status == ZX_OK) {
        ZX_DEBUG_ASSERT(actual <= count);
    }
    return fuchsia_io_FileReadAt_reply(txn, status, data, actual);
}

zx_status_t Connection::FileWrite(const uint8_t* data_data, size_t data_count, fidl_txn_t* txn) {
    FS_PRETTY_TRACE_DEBUG("[FileWrite] flags: ", ZxFlags(flags_));

    if (IsVnodeRefOnly(flags_)) {
        return fuchsia_io_FileWrite_reply(txn, ZX_ERR_BAD_HANDLE, 0);
    }
    if (!IsWritable(flags_)) {
        return fuchsia_io_FileWrite_reply(txn, ZX_ERR_BAD_HANDLE, 0);
    }

    size_t actual = 0;
    zx_status_t status;
    if (flags_ & ZX_FS_FLAG_APPEND) {
        size_t end;
        status = vnode_->Append(data_data, data_count, &end, &actual);
        if (status == ZX_OK) {
            offset_ = end;
        }
    } else {
        status = vnode_->Write(data_data, data_count, offset_, &actual);
        if (status == ZX_OK) {
            offset_ += actual;
        }
    }
    ZX_DEBUG_ASSERT(actual <= data_count);
    return fuchsia_io_FileWrite_reply(txn, status, actual);
}

zx_status_t Connection::FileWriteAt(const uint8_t* data_data, size_t data_count,
                                    uint64_t offset, fidl_txn_t* txn) {
    FS_PRETTY_TRACE_DEBUG("[FileWriteAt] flags: ", ZxFlags(flags_));

    if (IsVnodeRefOnly(flags_)) {
        return fuchsia_io_FileWriteAt_reply(txn, ZX_ERR_BAD_HANDLE, 0);
    }
    if (!IsWritable(flags_)) {
        return fuchsia_io_FileWriteAt_reply(txn, ZX_ERR_BAD_HANDLE, 0);
    }
    size_t actual = 0;
    zx_status_t status = vnode_->Write(data_data, data_count, offset, &actual);
    ZX_DEBUG_ASSERT(actual <= data_count);
    return fuchsia_io_FileWriteAt_reply(txn, status, actual);
}

zx_status_t Connection::FileSeek(int64_t offset, fuchsia_io_SeekOrigin start, fidl_txn_t* txn) {
    FS_PRETTY_TRACE_DEBUG("[FileSeek] flags: ", ZxFlags(flags_));

    static_assert(SEEK_SET == fuchsia_io_SeekOrigin_START, "");
    static_assert(SEEK_CUR == fuchsia_io_SeekOrigin_CURRENT, "");
    static_assert(SEEK_END == fuchsia_io_SeekOrigin_END, "");

    if (IsVnodeRefOnly(flags_)) {
        return fuchsia_io_FileSeek_reply(txn, ZX_ERR_BAD_HANDLE, offset_);
    }
    vnattr_t attr;
    zx_status_t r;
    if ((r = vnode_->Getattr(&attr)) < 0) {
        return r;
    }
    size_t n;
    switch (start) {
    case SEEK_SET:
        if (offset < 0) {
            return fuchsia_io_FileSeek_reply(txn, ZX_ERR_INVALID_ARGS, offset_);
        }
        n = offset;
        break;
    case SEEK_CUR:
        n = offset_ + offset;
        if (offset < 0) {
            // if negative seek
            if (n > offset_) {
                // wrapped around. attempt to seek before start
                return fuchsia_io_FileSeek_reply(txn, ZX_ERR_INVALID_ARGS, offset_);
            }
        } else {
            // positive seek
            if (n < offset_) {
                // wrapped around. overflow
                return fuchsia_io_FileSeek_reply(txn, ZX_ERR_INVALID_ARGS, offset_);
            }
        }
        break;
    case SEEK_END:
        n = attr.size + offset;
        if (offset < 0) {
            // if negative seek
            if (n > attr.size) {
                // wrapped around. attempt to seek before start
                return fuchsia_io_FileSeek_reply(txn, ZX_ERR_INVALID_ARGS, offset_);
            }
        } else {
            // positive seek
            if (n < attr.size) {
                // wrapped around
                return fuchsia_io_FileSeek_reply(txn, ZX_ERR_INVALID_ARGS, offset_);
            }
        }
        break;
    default:
        return fuchsia_io_FileSeek_reply(txn, ZX_ERR_INVALID_ARGS, offset_);
    }
    offset_ = n;
    return fuchsia_io_FileSeek_reply(txn, ZX_OK, offset_);
}

zx_status_t Connection::FileTruncate(uint64_t length, fidl_txn_t* txn) {
    FS_PRETTY_TRACE_DEBUG("[FileTruncate] flags: ", ZxFlags(flags_));

    if (IsVnodeRefOnly(flags_)) {
        return fuchsia_io_FileTruncate_reply(txn, ZX_ERR_BAD_HANDLE);
    }
    if (!IsWritable(flags_)) {
        return fuchsia_io_FileTruncate_reply(txn, ZX_ERR_BAD_HANDLE);
    }

    zx_status_t status = vnode_->Truncate(length);
    return fuchsia_io_FileTruncate_reply(txn, status);
}

zx_status_t Connection::FileGetFlags(fidl_txn_t* txn) {
    uint32_t flags = flags_ & (kStatusFlags | ZX_FS_RIGHTS);
    return fuchsia_io_FileGetFlags_reply(txn, ZX_OK, flags);
}

zx_status_t Connection::FileSetFlags(uint32_t flags, fidl_txn_t* txn) {
    flags_ = (flags_ & ~kSettableStatusFlags) | (flags & kSettableStatusFlags);
    return fuchsia_io_FileSetFlags_reply(txn, ZX_OK);
}

zx_status_t Connection::FileGetBuffer(uint32_t flags, fidl_txn_t* txn) {
    FS_PRETTY_TRACE_DEBUG("[FileGetBuffer] our flags: ", ZxFlags(flags_),
                          ", incoming flags: ", ZxFlags(flags));

    if (IsVnodeRefOnly(flags_)) {
        return fuchsia_io_FileGetBuffer_reply(txn, ZX_ERR_BAD_HANDLE, nullptr);
    }

    if ((flags & fuchsia_io_VMO_FLAG_PRIVATE) && (flags & fuchsia_io_VMO_FLAG_EXACT)) {
        return fuchsia_io_FileGetBuffer_reply(txn, ZX_ERR_INVALID_ARGS, nullptr);
    } else if ((flags_ & ZX_FS_FLAG_APPEND) && (flags & fuchsia_io_VMO_FLAG_WRITE)) {
        return fuchsia_io_FileGetBuffer_reply(txn, ZX_ERR_ACCESS_DENIED, nullptr);
    } else if (!IsWritable(flags_) && (flags & fuchsia_io_VMO_FLAG_WRITE)) {
        return fuchsia_io_FileGetBuffer_reply(txn, ZX_ERR_ACCESS_DENIED, nullptr);
    } else if (!IsReadable(flags_)) {
        return fuchsia_io_FileGetBuffer_reply(txn, ZX_ERR_ACCESS_DENIED, nullptr);
    }

    fuchsia_mem_Buffer buffer;
    memset(&buffer, 0, sizeof(buffer));
    zx_status_t status = vnode_->GetVmo(flags, &buffer.vmo, &buffer.size);
    return fuchsia_io_FileGetBuffer_reply(txn, status, status == ZX_OK ? &buffer : nullptr);
}

zx_status_t Connection::DirectoryOpen(uint32_t open_flags, uint32_t mode, const char* path_data,
                                      size_t path_size, zx_handle_t object) {
    FS_PRETTY_TRACE_DEBUG("[DirectoryOpen] our flags: ", ZxFlags(flags_),
                          ", incoming flags: ", ZxFlags(open_flags),
                          ", path: ", Path(path_data, path_size));

    zx::channel channel(object);
    auto write_error = [describe = open_flags & ZX_FS_FLAG_DESCRIBE](zx::channel channel,
                                                                     zx_status_t error) {
        if (describe) {
            WriteDescribeError(std::move(channel), error);
        }
        return ZX_OK;
    };
    if (IsVnodeRefOnly(flags_)) {
        return write_error(std::move(channel), ZX_ERR_BAD_HANDLE);
    }
    if (open_flags & ZX_FS_FLAG_CLONE_SAME_RIGHTS) {
        return write_error(std::move(channel), ZX_ERR_INVALID_ARGS);
    }
    if (!IsVnodeRefOnly(open_flags) && !(open_flags & ZX_FS_RIGHTS)) {
        return write_error(std::move(channel), ZX_ERR_INVALID_ARGS);
    }
    if ((path_size < 1) || (path_size > PATH_MAX)) {
        return write_error(std::move(channel), ZX_ERR_BAD_PATH);
    }

    // Check for directory rights inheritance
    zx_status_t status = EnforceHierarchicalRights(flags_, open_flags, &open_flags);
    if (status != ZX_OK) {
        FS_PRETTY_TRACE_DEBUG("Rights violation during DirectoryOpen");
        return write_error(std::move(channel), status);
    }
    OpenAt(vfs_, vnode_, std::move(channel), fbl::StringPiece(path_data, path_size),
           open_flags, flags_ & ZX_FS_RIGHTS, mode);
    return ZX_OK;
}

zx_status_t Connection::DirectoryUnlink(const char* path_data, size_t path_size, fidl_txn_t* txn) {
    FS_PRETTY_TRACE_DEBUG("[DirectoryUnlink] our flags: ", ZxFlags(flags_),
                          ", path: ", Path(path_data, path_size));

    if (IsVnodeRefOnly(flags_)) {
        return fuchsia_io_DirectoryUnlink_reply(txn, ZX_ERR_BAD_HANDLE);
    }
    if (!IsWritable(flags_)) {
        return fuchsia_io_DirectoryUnlink_reply(txn, ZX_ERR_BAD_HANDLE);
    }
    zx_status_t status = vfs_->Unlink(vnode_, fbl::StringPiece(path_data, path_size));
    return fuchsia_io_DirectoryUnlink_reply(txn, status);
}

zx_status_t Connection::DirectoryReadDirents(uint64_t max_out, fidl_txn_t* txn) {
    FS_PRETTY_TRACE_DEBUG("[DirectoryReadDirents] our flags: ", ZxFlags(flags_));

    if (IsVnodeRefOnly(flags_)) {
        return fuchsia_io_DirectoryReadDirents_reply(txn, ZX_ERR_BAD_HANDLE, nullptr, 0);
    }
    if (max_out > ZXFIDL_MAX_MSG_BYTES) {
        return fuchsia_io_DirectoryReadDirents_reply(txn, ZX_ERR_BAD_HANDLE, nullptr, 0);
    }
    uint8_t data[max_out];
    size_t actual = 0;
    zx_status_t status = vfs_->Readdir(vnode_.get(), &dircookie_, data, max_out, &actual);
    return fuchsia_io_DirectoryReadDirents_reply(txn, status, data, actual);
}

zx_status_t Connection::DirectoryRewind(fidl_txn_t* txn) {
    FS_PRETTY_TRACE_DEBUG("[DirectoryRewind] our flags: ", ZxFlags(flags_));

    if (IsVnodeRefOnly(flags_)) {
        return fuchsia_io_DirectoryRewind_reply(txn, ZX_ERR_BAD_HANDLE);
    }
    dircookie_.Reset();
    return fuchsia_io_DirectoryRewind_reply(txn, ZX_OK);
}

zx_status_t Connection::DirectoryGetToken(fidl_txn_t* txn) {
    FS_PRETTY_TRACE_DEBUG("[DirectoryGetToken] our flags: ", ZxFlags(flags_));

    if (!IsWritable(flags_)) {
        return fuchsia_io_DirectoryGetToken_reply(txn, ZX_ERR_BAD_HANDLE, ZX_HANDLE_INVALID);
    }
    zx::event returned_token;
    zx_status_t status = vfs_->VnodeToToken(vnode_, &token_, &returned_token);
    return fuchsia_io_DirectoryGetToken_reply(txn, status, returned_token.release());
}

zx_status_t Connection::DirectoryRename(const char* src_data, size_t src_size,
                                        zx_handle_t dst_parent_token, const char* dst_data,
                                        size_t dst_size, fidl_txn_t* txn) {
    FS_PRETTY_TRACE_DEBUG("[DirectoryRename] our flags: ", ZxFlags(flags_),
                          ", src: ", Path(src_data, src_size),
                          ", dst: ", Path(dst_data, dst_size));

    zx::event token(dst_parent_token);
    fbl::StringPiece oldStr(src_data, src_size);
    fbl::StringPiece newStr(dst_data, dst_size);

    if (src_size < 1 || dst_size < 1) {
        return fuchsia_io_DirectoryRename_reply(txn, ZX_ERR_INVALID_ARGS);
    }
    if (IsVnodeRefOnly(flags_)) {
        return fuchsia_io_DirectoryRename_reply(txn, ZX_ERR_BAD_HANDLE);
    }
    if (!IsWritable(flags_)) {
        return fuchsia_io_DirectoryRename_reply(txn, ZX_ERR_BAD_HANDLE);
    }
    zx_status_t status = vfs_->Rename(std::move(token), vnode_,
                                      std::move(oldStr), std::move(newStr));
    return fuchsia_io_DirectoryRename_reply(txn, status);
}

zx_status_t Connection::DirectoryLink(const char* src_data, size_t src_size,
                                      zx_handle_t dst_parent_token, const char* dst_data,
                                      size_t dst_size, fidl_txn_t* txn) {
    FS_PRETTY_TRACE_DEBUG("[DirectoryLink] our flags: ", ZxFlags(flags_),
                          ", src: ", Path(src_data, src_size),
                          ", dst: ", Path(dst_data, dst_size));

    zx::event token(dst_parent_token);
    fbl::StringPiece oldStr(src_data, src_size);
    fbl::StringPiece newStr(dst_data, dst_size);

    if (src_size < 1 || dst_size < 1) {
        return fuchsia_io_DirectoryLink_reply(txn, ZX_ERR_INVALID_ARGS);
    }
    if (IsVnodeRefOnly(flags_)) {
        return fuchsia_io_DirectoryLink_reply(txn, ZX_ERR_BAD_HANDLE);
    }
    if (!IsWritable(flags_)) {
        return fuchsia_io_DirectoryLink_reply(txn, ZX_ERR_BAD_HANDLE);
    }
    zx_status_t status = vfs_->Link(std::move(token), vnode_, std::move(oldStr),
                                    std::move(newStr));
    return fuchsia_io_DirectoryLink_reply(txn, status);
}

zx_status_t Connection::DirectoryWatch(uint32_t mask, uint32_t options, zx_handle_t handle,
                                       fidl_txn_t* txn) {
    FS_PRETTY_TRACE_DEBUG("[DirectoryWatch] our flags: ", ZxFlags(flags_));

    if (IsVnodeRefOnly(flags_)) {
        return fuchsia_io_DirectoryWatch_reply(txn, ZX_ERR_BAD_HANDLE);
    }
    zx::channel watcher(handle);
    zx_status_t status = vnode_->WatchDir(vfs_, mask, options, std::move(watcher));
    return fuchsia_io_DirectoryWatch_reply(txn, status);
}

zx_status_t Connection::DirectoryAdminMount(zx_handle_t remote, fidl_txn_t* txn) {
    FS_PRETTY_TRACE_DEBUG("[DirectoryAdminMount] our flags: ", ZxFlags(flags_));

    if (!HasAdminRight(flags_)) {
        vfs_unmount_handle(remote, 0);
        return fuchsia_io_DirectoryAdminMount_reply(txn, ZX_ERR_ACCESS_DENIED);
    }
    MountChannel c = MountChannel(remote);
    zx_status_t status = vfs_->InstallRemote(vnode_, std::move(c));
    return fuchsia_io_DirectoryAdminMount_reply(txn, status);
}

zx_status_t Connection::DirectoryAdminMountAndCreate(zx_handle_t remote, const char* name,
                                                     size_t name_size, uint32_t flags,
                                                     fidl_txn_t* txn) {
    FS_PRETTY_TRACE_DEBUG("[DirectoryAdminMountAndCreate] our flags: ", ZxFlags(flags_));

    if (!HasAdminRight(flags_)) {
        vfs_unmount_handle(remote, 0);
        return fuchsia_io_DirectoryAdminMount_reply(txn, ZX_ERR_ACCESS_DENIED);
    }
    fbl::StringPiece str(name, name_size);
    zx_status_t status = vfs_->MountMkdir(vnode_, std::move(str), MountChannel(remote), flags);
    return fuchsia_io_DirectoryAdminMount_reply(txn, status);
}

zx_status_t Connection::DirectoryAdminUnmount(fidl_txn_t* txn) {
    FS_PRETTY_TRACE_DEBUG("[DirectoryAdminUnmount] our flags: ", ZxFlags(flags_));

    if (!HasAdminRight(flags_)) {
        return fuchsia_io_DirectoryAdminUnmount_reply(txn, ZX_ERR_ACCESS_DENIED);
    }
    vfs_->UninstallAll(ZX_TIME_INFINITE);

    // Unmount is fatal to the requesting connections.
    Vfs::ShutdownCallback closure([ch = std::move(channel_),
                                   ctxn = FidlConnection::CopyTxn(txn)](zx_status_t status) mutable {
        fuchsia_io_DirectoryAdminUnmount_reply(ctxn.Txn(), status);
    });
    Vfs* vfs = vfs_;
    Terminate(/* call_close= */ true);
    vfs->Shutdown(std::move(closure));
    return ERR_DISPATCHER_ASYNC;
}

zx_status_t Connection::DirectoryAdminUnmountNode(fidl_txn_t* txn) {
    FS_PRETTY_TRACE_DEBUG("[DirectoryAdminUnmountNode] our flags: ", ZxFlags(flags_));

    if (!HasAdminRight(flags_)) {
        return fuchsia_io_DirectoryAdminUnmountNode_reply(txn,
                                                          ZX_ERR_ACCESS_DENIED,
                                                          ZX_HANDLE_INVALID);
    }
    zx::channel c;
    zx_status_t status = vfs_->UninstallRemote(vnode_, &c);
    return fuchsia_io_DirectoryAdminUnmountNode_reply(txn, status, c.release());
}

zx_status_t Connection::DirectoryAdminQueryFilesystem(fidl_txn_t* txn) {
    FS_PRETTY_TRACE_DEBUG("[DirectoryAdminQueryFilesystem] our flags: ", ZxFlags(flags_));

    fuchsia_io_FilesystemInfo info;
    zx_status_t status = vnode_->QueryFilesystem(&info);
    return fuchsia_io_DirectoryAdminQueryFilesystem_reply(txn, status,
                                                          status == ZX_OK ? &info : nullptr);
}

zx_status_t Connection::DirectoryAdminGetDevicePath(fidl_txn_t* txn) {
    FS_PRETTY_TRACE_DEBUG("[DirectoryAdminGetDevicePath] our flags: ", ZxFlags(flags_));

    if (!HasAdminRight(flags_)) {
        return fuchsia_io_DirectoryAdminGetDevicePath_reply(txn, ZX_ERR_ACCESS_DENIED, nullptr, 0);
    }

    char name[fuchsia_io_MAX_PATH];
    size_t actual = 0;
    zx_status_t status = vnode_->GetDevicePath(sizeof(name), name, &actual);
    return fuchsia_io_DirectoryAdminGetDevicePath_reply(txn, status, name, actual);
}

zx_status_t Connection::HandleFsSpecificMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    zx_handle_close_many(msg->handles, msg->num_handles);
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Connection::HandleMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    zx_status_t status = fuchsia_io_Node_try_dispatch(this, txn, msg, &kNodeOps);
    if (status != ZX_ERR_NOT_SUPPORTED) {
        return status;
    }
    status = fuchsia_io_File_try_dispatch(this, txn, msg, &kFileOps);
    if (status != ZX_ERR_NOT_SUPPORTED) {
        return status;
    }
    status = fuchsia_io_Directory_try_dispatch(this, txn, msg, &kDirectoryOps);
    if (status != ZX_ERR_NOT_SUPPORTED) {
        return status;
    }
    status = fuchsia_io_DirectoryAdmin_try_dispatch(this, txn, msg, &kDirectoryAdminOps);
    if (status != ZX_ERR_NOT_SUPPORTED) {
        return status;
    }
    return HandleFsSpecificMessage(msg, txn);
}

} // namespace fs
