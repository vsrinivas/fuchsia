// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/io.h>
#include <lib/fdio/vfs.h>
#include <lib/fidl/txn_header.h>
#include <lib/zircon-internal/debug.h>
#include <lib/zx/handle.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zircon/assert.h>

#include <memory>
#include <type_traits>
#include <utility>

#include <fbl/string_buffer.h>
#include <fs/debug.h>
#include <fs/handler.h>
#include <fs/internal/connection.h>
#include <fs/trace.h>
#include <fs/vfs_types.h>
#include <fs/vnode.h>

static_assert(fuchsia_io_OPEN_FLAGS_ALLOWED_WITH_NODE_REFERENCE ==
                  (fuchsia_io_OPEN_FLAG_DIRECTORY | fuchsia_io_OPEN_FLAG_NOT_DIRECTORY |
                   fuchsia_io_OPEN_FLAG_DESCRIBE | fuchsia_io_OPEN_FLAG_NODE_REFERENCE),
              "OPEN_FLAGS_ALLOWED_WITH_NODE_REFERENCE value mismatch");
static_assert(PATH_MAX == fuchsia_io_MAX_PATH, "POSIX PATH_MAX inconsistent with Fuchsia MAX_PATH");
static_assert(NAME_MAX == fuchsia_io_MAX_FILENAME,
              "POSIX NAME_MAX inconsistent with Fuchsia MAX_FILENAME");

namespace fs {

namespace {

void WriteDescribeError(zx::channel channel, zx_status_t status) {
  fuchsia_io_NodeOnOpenEvent msg;
  memset(&msg, 0, sizeof(msg));
  fidl_init_txn_header(&msg.hdr, 0, fuchsia_io_NodeOnOpenOrdinal);
  msg.s = status;
  channel.write(0, &msg, sizeof(msg), nullptr, 0);
}

zx_status_t GetNodeInfo(const fbl::RefPtr<Vnode>& vn, VnodeConnectionOptions options,
                        fuchsia_io_NodeInfo* info) {
  if (options.flags.node_reference) {
    info->tag = fuchsia_io_NodeInfoTag_service;
    return ZX_OK;
  } else {
    fs::VnodeRepresentation representation;
    zx_status_t status = vn->GetNodeInfo(options.rights, &representation);
    if (status != ZX_OK) {
      return status;
    }
    representation.visit([info](auto&& repr) {
      using T = std::decay_t<decltype(repr)>;
      if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Connector>) {
        info->tag = fuchsia_io_NodeInfoTag_service;
      } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::File>) {
        info->tag = fuchsia_io_NodeInfoTag_file;
        info->file.event = repr.observer.release();
      } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Directory>) {
        info->tag = fuchsia_io_NodeInfoTag_directory;
      } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Pipe>) {
        info->tag = fuchsia_io_NodeInfoTag_pipe;
        info->pipe.socket = repr.socket.release();
      } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Memory>) {
        info->tag = fuchsia_io_NodeInfoTag_vmofile;
        info->vmofile.vmo = repr.vmo.release();
        info->vmofile.offset = repr.offset;
        info->vmofile.length = repr.length;
      } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Device>) {
        info->tag = fuchsia_io_NodeInfoTag_device;
        info->device.event = repr.event.release();
      } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Tty>) {
        info->tag = fuchsia_io_NodeInfoTag_tty;
        info->device.event = repr.event.release();
      } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Socket>) {
        info->tag = fuchsia_io_NodeInfoTag_socket;
        info->socket.socket = repr.socket.release();
      } else {
        ZX_ASSERT_MSG(false, "Representation variant is not initialized");
      }
    });
    return ZX_OK;
  }
}

void Describe(const fbl::RefPtr<Vnode>& vn, VnodeConnectionOptions options, OnOpenMsg* response,
              zx_handle_t* handle) {
  fidl_init_txn_header(&response->primary.hdr, 0, fuchsia_io_NodeOnOpenOrdinal);
  response->extra.file.event = ZX_HANDLE_INVALID;
  zx_status_t r = GetNodeInfo(vn, options, &response->extra);

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
    case fuchsia_io_NodeInfoTag_socket:
      encode_handle(&response->extra.socket.socket, handle);
      break;
    default:
      ZX_DEBUG_ASSERT_MSG(false, "Unsupported NodeInfoTag: %d\n", response->extra.tag);
  }

  // If a valid response was returned, encode it.
  response->primary.s = r;
  response->primary.info =
      reinterpret_cast<fuchsia_io_NodeInfo*>(r == ZX_OK ? FIDL_ALLOC_PRESENT : FIDL_ALLOC_ABSENT);
}

// Perform basic flags sanitization.
// Returns false if the flags combination is invalid.
bool PrevalidateFlags(uint32_t flags) {
  // If the caller specified an unknown right, reject the request.
  if ((flags & ZX_FS_RIGHTS_SPACE) & ~ZX_FS_RIGHTS) {
    return false;
  }

  if (flags & fuchsia_io_OPEN_FLAG_NODE_REFERENCE) {
    constexpr uint32_t kValidFlagsForNodeRef =
        fuchsia_io_OPEN_FLAG_NODE_REFERENCE | fuchsia_io_OPEN_FLAG_DIRECTORY |
        fuchsia_io_OPEN_FLAG_NOT_DIRECTORY | fuchsia_io_OPEN_FLAG_DESCRIBE;
    // Explicitly reject VNODE_REF_ONLY together with any invalid flags.
    if (flags & ~kValidFlagsForNodeRef) {
      return false;
    }
  }
  return true;
}

zx_status_t EnforceHierarchicalRights(Rights parent_rights, VnodeConnectionOptions child_options,
                                      VnodeConnectionOptions* out_options) {
  if (child_options.flags.posix) {
    if (!parent_rights.write && !child_options.rights.write && !parent_rights.execute &&
        !child_options.rights.execute) {
      // Posix compatibility flag allows the child dir connection to inherit every right from
      // its immediate parent. Here we know there exists a read-only directory somewhere along
      // the Open() chain, so remove this flag to rid the child connection the ability to
      // inherit read-write right from e.g. crossing a read-write mount point
      // down the line, or similarly with the execute right.
      child_options.flags.posix = false;
    }
  }
  if (!child_options.rights.StricterOrSameAs(parent_rights)) {
    // Client asked for some right but we do not have it
    return ZX_ERR_ACCESS_DENIED;
  }
  *out_options = child_options;
  return ZX_OK;
}

// Performs a path walk and opens a connection to another node.
void OpenAt(Vfs* vfs, const fbl::RefPtr<Vnode>& parent, zx::channel channel, fbl::StringPiece path,
            VnodeConnectionOptions options, Rights parent_rights, uint32_t mode) {
  bool describe = options.flags.describe;
  vfs->Open(std::move(parent), path, options, parent_rights, mode).visit([&](auto&& result) {
    using ResultT = std::decay_t<decltype(result)>;
    using OpenResult = fs::Vfs::OpenResult;
    if constexpr (std::is_same_v<ResultT, OpenResult::Error>) {
      FS_TRACE_DEBUG("vfs: open failure: %d\n", result);
      if (describe) {
        WriteDescribeError(std::move(channel), result);
      }
    } else if constexpr (std::is_same_v<ResultT, OpenResult::Remote>) {
      FS_TRACE_DEBUG("vfs: handoff to remote\n");
      // Remote handoff to a remote filesystem node.
      vfs->ForwardOpenRemote(std::move(result.vnode), std::move(channel), result.path, options,
                             mode);
    } else if constexpr (std::is_same_v<ResultT, OpenResult::RemoteRoot>) {
      FS_TRACE_DEBUG("vfs: handoff to remote\n");
      // Remote handoff to a remote filesystem node.
      vfs->ForwardOpenRemote(std::move(result.vnode), std::move(channel), ".", options, mode);
    } else if constexpr (std::is_same_v<ResultT, OpenResult::Ok>) {
      if (describe) {
        OnOpenMsg response;
        memset(&response, 0, sizeof(response));
        zx_handle_t extra = ZX_HANDLE_INVALID;
        Describe(result.vnode, options, &response, &extra);
        uint32_t hcount = (extra != ZX_HANDLE_INVALID) ? 1 : 0;
        channel.write(0, &response, sizeof(OnOpenMsg), &extra, hcount);
      }
      // |Vfs::Open| already performs option validation for us.
      vfs->Serve(result.vnode, std::move(channel), result.validated_options);
    }
  });
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
#define ZXFIDL_OPERATION(Method)                                                    \
  template <typename... Args>                                                       \
  zx_status_t Method##Op(void* ctx, Args... args) {                                 \
    TRACE_DURATION("vfs", #Method);                                                 \
    auto connection = reinterpret_cast<internal::Connection*>(ctx);                 \
    return (connection->internal::Connection::Method)(std::forward<Args>(args)...); \
  }

ZXFIDL_OPERATION(NodeClone)
ZXFIDL_OPERATION(NodeClose)
ZXFIDL_OPERATION(NodeDescribe)
ZXFIDL_OPERATION(NodeSync)
ZXFIDL_OPERATION(NodeGetAttr)
ZXFIDL_OPERATION(NodeSetAttr)
ZXFIDL_OPERATION(NodeNodeGetFlags)
ZXFIDL_OPERATION(NodeNodeSetFlags)

const fuchsia_io_Node_ops kNodeOps = {
    .Clone = NodeCloneOp,
    .Close = NodeCloseOp,
    .Describe = NodeDescribeOp,
    .Sync = NodeSyncOp,
    .GetAttr = NodeGetAttrOp,
    .SetAttr = NodeSetAttrOp,
    .NodeGetFlags = NodeNodeGetFlagsOp,
    .NodeSetFlags = NodeNodeSetFlagsOp
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
    .NodeGetFlags = NodeNodeGetFlagsOp,
    .NodeSetFlags = NodeNodeSetFlagsOp,
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
    .NodeGetFlags = NodeNodeGetFlagsOp,
    .NodeSetFlags = NodeNodeSetFlagsOp,
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
    .NodeGetFlags = NodeNodeGetFlagsOp,
    .NodeSetFlags = NodeNodeSetFlagsOp,
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

}  // namespace

constexpr zx_signals_t kWakeSignals =
    ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED | kLocalTeardownSignal;

// Flags which can be modified by SetFlags.
constexpr uint32_t kSettableStatusFlags = fuchsia_io_OPEN_FLAG_APPEND;

// All flags which indicate state of the connection (excluding rights).
constexpr uint32_t kStatusFlags = kSettableStatusFlags | fuchsia_io_OPEN_FLAG_NODE_REFERENCE;

namespace internal {

Connection::Connection(Vfs* vfs, fbl::RefPtr<Vnode> vnode, zx::channel channel,
                       VnodeConnectionOptions options)
    : vfs_(vfs),
      vnode_(std::move(vnode)),
      channel_(std::move(channel)),
      wait_(this, ZX_HANDLE_INVALID, kWakeSignals),
      options_(VnodeConnectionOptions::FilterForNewConnection(options)) {
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
  CloseMessage(
      [this](fidl_msg_t* msg, FidlConnection* txn) { return HandleMessage(msg, txn->Txn()); });
  set_closed();
}

zx_status_t Connection::NodeClone(uint32_t clone_flags, zx_handle_t object) {
  zx::channel channel(object);
  auto clone_options = VnodeConnectionOptions::FromIoV1Flags(clone_flags);
  auto write_error = [describe = clone_options.flags.describe](zx::channel channel,
                                                               zx_status_t error) {
    if (describe) {
      WriteDescribeError(std::move(channel), error);
    }
    return ZX_OK;
  };
  if (!PrevalidateFlags(clone_flags)) {
    FS_PRETTY_TRACE_DEBUG("[NodeClone] prevalidate failed",
                          ", incoming flags: ", ZxFlags(clone_flags));
    return write_error(std::move(channel), ZX_ERR_INVALID_ARGS);
  }
  FS_PRETTY_TRACE_DEBUG("[NodeClone] our options: ", options_,
                        ", incoming options: ", clone_options);

  bool describe = clone_options.flags.describe;
  // If CLONE_SAME_RIGHTS is specified, the client cannot request any specific rights.
  if (clone_options.flags.clone_same_rights && clone_options.rights.any()) {
    return write_error(std::move(channel), ZX_ERR_INVALID_ARGS);
  }
  // These two flags are always preserved.
  clone_options.flags.append = options_.flags.append;
  clone_options.flags.node_reference = options_.flags.node_reference;
  // If CLONE_SAME_RIGHTS is requested, cloned connection will inherit the same rights
  // as those from the originating connection.
  if (clone_options.flags.clone_same_rights) {
    clone_options.rights = options_.rights;
  }
  if (!clone_options.rights.StricterOrSameAs(options_.rights)) {
    FS_PRETTY_TRACE_DEBUG("Rights violation during NodeClone");
    return write_error(std::move(channel), ZX_ERR_ACCESS_DENIED);
  }

  fbl::RefPtr<Vnode> vn(vnode_);
  auto result = vn->ValidateOptions(clone_options);
  if (result.is_error()) {
    return write_error(std::move(channel), result.error());
  }
  auto& validated_options = result.value();
  zx_status_t open_status = ZX_OK;
  if (!clone_options.flags.node_reference) {
    open_status = OpenVnode(validated_options, &vn);
  }
  if (describe) {
    OnOpenMsg response;
    memset(&response, 0, sizeof(response));
    response.primary.s = open_status;
    zx_handle_t extra = ZX_HANDLE_INVALID;
    if (open_status == ZX_OK) {
      Describe(vnode_, clone_options, &response, &extra);
    }
    uint32_t hcount = (extra != ZX_HANDLE_INVALID) ? 1 : 0;
    channel.write(0, &response, sizeof(OnOpenMsg), &extra, hcount);
  }

  if (open_status == ZX_OK) {
    vfs_->Serve(vn, std::move(channel), validated_options);
  }
  return ZX_OK;
}

zx_status_t Connection::NodeClose(fidl_txn_t* txn) {
  zx_status_t status;
  if (options_.flags.node_reference) {
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
  zx_status_t status = GetNodeInfo(vnode_, options_, &info);
  if (status != ZX_OK) {
    return status;
  }
  return fuchsia_io_NodeDescribe_reply(txn, &info);
}

zx_status_t Connection::NodeSync(fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[NodeSync] options: ", options_);

  if (options_.flags.node_reference) {
    return fuchsia_io_NodeSync_reply(txn, ZX_ERR_BAD_HANDLE);
  }
  Vnode::SyncCallback closure(
      [this, ctxn = FidlConnection::CopyTxn(txn)](zx_status_t status) mutable {
        fuchsia_io_NodeSync_reply(ctxn.Txn(), status);

        // Try to reset the wait object
        ZX_ASSERT_MSG(wait_.Begin(vfs_->dispatcher()) == ZX_OK, "Dispatch loop unexpectedly ended");
      });

  vnode_->Sync(std::move(closure));
  return ERR_DISPATCHER_ASYNC;
}

zx_status_t Connection::NodeGetAttr(fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[NodeGetAttr] options: ", options_);

  fuchsia_io_NodeAttributes attributes;
  memset(&attributes, 0, sizeof(attributes));

  fs::VnodeAttributes attr;
  zx_status_t r;
  if ((r = vnode_->GetAttributes(&attr)) != ZX_OK) {
    return fuchsia_io_NodeGetAttr_reply(txn, r, &attributes);
  }

  attributes.mode = attr.mode;
  attributes.id = attr.inode;
  attributes.content_size = attr.content_size;
  attributes.storage_size = attr.storage_size;
  attributes.link_count = attr.link_count;
  attributes.creation_time = attr.creation_time;
  attributes.modification_time = attr.modification_time;

  return fuchsia_io_NodeGetAttr_reply(txn, ZX_OK, &attributes);
}

zx_status_t Connection::NodeSetAttr(uint32_t flags, const fuchsia_io_NodeAttributes* attributes,
                                    fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[NodeSetAttr] our options: ", options_, ", incoming flags: ", flags);

  if (options_.flags.node_reference) {
    return fuchsia_io_NodeSetAttr_reply(txn, ZX_ERR_BAD_HANDLE);
  }
  if (!options_.rights.write) {
    return fuchsia_io_NodeSetAttr_reply(txn, ZX_ERR_BAD_HANDLE);
  }
  constexpr uint32_t supported_flags = fuchsia_io_NODE_ATTRIBUTE_FLAG_CREATION_TIME |
                                       fuchsia_io_NODE_ATTRIBUTE_FLAG_MODIFICATION_TIME;
  if (flags & ~supported_flags) {
    return fuchsia_io_NodeSetAttr_reply(txn, ZX_ERR_INVALID_ARGS);
  }

  zx_status_t status = vnode_->SetAttributes(
      fs::VnodeAttributesUpdate()
          .set_creation_time(flags & fuchsia_io_NODE_ATTRIBUTE_FLAG_CREATION_TIME
                                 ? std::make_optional(attributes->creation_time)
                                 : std::nullopt)
          .set_modification_time(flags & fuchsia_io_NODE_ATTRIBUTE_FLAG_MODIFICATION_TIME
                                     ? std::make_optional(attributes->modification_time)
                                     : std::nullopt));
  return fuchsia_io_NodeSetAttr_reply(txn, status);
}

zx_status_t Connection::NodeNodeGetFlags(fidl_txn_t* txn) {
  uint32_t flags = options_.ToIoV1Flags() & (kStatusFlags | ZX_FS_RIGHTS);
  return fuchsia_io_NodeNodeGetFlags_reply(txn, ZX_OK, flags);
}

zx_status_t Connection::NodeNodeSetFlags(uint32_t flags, fidl_txn_t* txn) {
  auto options = VnodeConnectionOptions::FromIoV1Flags(flags);
  options_.flags.append = options.flags.append;
  return fuchsia_io_NodeNodeSetFlags_reply(txn, ZX_OK);
}

zx_status_t Connection::FileRead(uint64_t count, fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[FileRead] options: ", options_);

  if (options_.flags.node_reference) {
    return fuchsia_io_FileRead_reply(txn, ZX_ERR_BAD_HANDLE, nullptr, 0);
  } else if (!options_.rights.read) {
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
  FS_PRETTY_TRACE_DEBUG("[FileReadAt] options: ", options_);

  if (options_.flags.node_reference) {
    return fuchsia_io_FileReadAt_reply(txn, ZX_ERR_BAD_HANDLE, nullptr, 0);
  } else if (!options_.rights.read) {
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
  FS_PRETTY_TRACE_DEBUG("[FileWrite] options: ", options_);

  if (options_.flags.node_reference) {
    return fuchsia_io_FileWrite_reply(txn, ZX_ERR_BAD_HANDLE, 0);
  }
  if (!options_.rights.write) {
    return fuchsia_io_FileWrite_reply(txn, ZX_ERR_BAD_HANDLE, 0);
  }

  size_t actual = 0;
  zx_status_t status;
  if (options_.flags.append) {
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

zx_status_t Connection::FileWriteAt(const uint8_t* data_data, size_t data_count, uint64_t offset,
                                    fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[FileWriteAt] options: ", options_);

  if (options_.flags.node_reference) {
    return fuchsia_io_FileWriteAt_reply(txn, ZX_ERR_BAD_HANDLE, 0);
  }
  if (!options_.rights.write) {
    return fuchsia_io_FileWriteAt_reply(txn, ZX_ERR_BAD_HANDLE, 0);
  }
  size_t actual = 0;
  zx_status_t status = vnode_->Write(data_data, data_count, offset, &actual);
  ZX_DEBUG_ASSERT(actual <= data_count);
  return fuchsia_io_FileWriteAt_reply(txn, status, actual);
}

zx_status_t Connection::FileSeek(int64_t offset, fuchsia_io_SeekOrigin start, fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[FileSeek] options: ", options_);

  static_assert(SEEK_SET == fuchsia_io_SeekOrigin_START, "");
  static_assert(SEEK_CUR == fuchsia_io_SeekOrigin_CURRENT, "");
  static_assert(SEEK_END == fuchsia_io_SeekOrigin_END, "");

  if (options_.flags.node_reference) {
    return fuchsia_io_FileSeek_reply(txn, ZX_ERR_BAD_HANDLE, offset_);
  }
  fs::VnodeAttributes attr;
  zx_status_t r;
  if ((r = vnode_->GetAttributes(&attr)) < 0) {
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
      n = attr.content_size + offset;
      if (offset < 0) {
        // if negative seek
        if (n > attr.content_size) {
          // wrapped around. attempt to seek before start
          return fuchsia_io_FileSeek_reply(txn, ZX_ERR_INVALID_ARGS, offset_);
        }
      } else {
        // positive seek
        if (n < attr.content_size) {
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
  FS_PRETTY_TRACE_DEBUG("[FileTruncate] options: ", options_);

  if (options_.flags.node_reference) {
    return fuchsia_io_FileTruncate_reply(txn, ZX_ERR_BAD_HANDLE);
  }
  if (!options_.rights.write) {
    return fuchsia_io_FileTruncate_reply(txn, ZX_ERR_BAD_HANDLE);
  }

  zx_status_t status = vnode_->Truncate(length);
  return fuchsia_io_FileTruncate_reply(txn, status);
}

zx_status_t Connection::FileGetFlags(fidl_txn_t* txn) {
  uint32_t flags = options_.ToIoV1Flags() & (kStatusFlags | ZX_FS_RIGHTS);
  return fuchsia_io_FileGetFlags_reply(txn, ZX_OK, flags);
}

zx_status_t Connection::FileSetFlags(uint32_t flags, fidl_txn_t* txn) {
  auto options = VnodeConnectionOptions::FromIoV1Flags(flags);
  options_.flags.append = options.flags.append;
  return fuchsia_io_FileSetFlags_reply(txn, ZX_OK);
}

zx_status_t Connection::FileGetBuffer(uint32_t flags, fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[FileGetBuffer] our options: ", options_,
                        ", incoming flags: ", ZxFlags(flags));

  if (options_.flags.node_reference) {
    return fuchsia_io_FileGetBuffer_reply(txn, ZX_ERR_BAD_HANDLE, nullptr);
  }

  if ((flags & fuchsia_io_VMO_FLAG_PRIVATE) && (flags & fuchsia_io_VMO_FLAG_EXACT)) {
    return fuchsia_io_FileGetBuffer_reply(txn, ZX_ERR_INVALID_ARGS, nullptr);
  } else if ((options_.flags.append) && (flags & fuchsia_io_VMO_FLAG_WRITE)) {
    return fuchsia_io_FileGetBuffer_reply(txn, ZX_ERR_ACCESS_DENIED, nullptr);
  } else if (!options_.rights.write && (flags & fuchsia_io_VMO_FLAG_WRITE)) {
    return fuchsia_io_FileGetBuffer_reply(txn, ZX_ERR_ACCESS_DENIED, nullptr);
  } else if (!options_.rights.execute && (flags & fuchsia_io_VMO_FLAG_EXEC)) {
    return fuchsia_io_FileGetBuffer_reply(txn, ZX_ERR_ACCESS_DENIED, nullptr);
  } else if (!options_.rights.read) {
    return fuchsia_io_FileGetBuffer_reply(txn, ZX_ERR_ACCESS_DENIED, nullptr);
  }

  fuchsia_mem_Buffer buffer;
  memset(&buffer, 0, sizeof(buffer));
  zx_status_t status = vnode_->GetVmo(flags, &buffer.vmo, &buffer.size);
  return fuchsia_io_FileGetBuffer_reply(txn, status, status == ZX_OK ? &buffer : nullptr);
}

zx_status_t Connection::DirectoryOpen(uint32_t open_flags, uint32_t mode, const char* path_data,
                                      size_t path_size, zx_handle_t object) {
  zx::channel channel(object);
  auto open_options = VnodeConnectionOptions::FromIoV1Flags(open_flags);
  auto write_error = [describe = open_options.flags.describe](zx::channel channel,
                                                              zx_status_t error) {
    if (describe) {
      WriteDescribeError(std::move(channel), error);
    }
    return ZX_OK;
  };

  if (!PrevalidateFlags(open_flags)) {
    FS_PRETTY_TRACE_DEBUG("[DirectoryOpen] prevalidate failed",
                          ", incoming flags: ", ZxFlags(open_flags),
                          ", path: ", Path(path_data, path_size));
    if (open_options.flags.describe) {
      return write_error(std::move(channel), ZX_ERR_INVALID_ARGS);
    }
  }

  FS_PRETTY_TRACE_DEBUG("[DirectoryOpen] our options: ", options_,
                        ", incoming options: ", open_options,
                        ", path: ", Path(path_data, path_size));
  if (options_.flags.node_reference) {
    return write_error(std::move(channel), ZX_ERR_BAD_HANDLE);
  }
  if (open_options.flags.clone_same_rights) {
    return write_error(std::move(channel), ZX_ERR_INVALID_ARGS);
  }
  if (!open_options.flags.node_reference && !open_options.rights.any()) {
    return write_error(std::move(channel), ZX_ERR_INVALID_ARGS);
  }
  if ((path_size < 1) || (path_size > PATH_MAX)) {
    return write_error(std::move(channel), ZX_ERR_BAD_PATH);
  }

  // Check for directory rights inheritance
  zx_status_t status = EnforceHierarchicalRights(options_.rights, open_options, &open_options);
  if (status != ZX_OK) {
    FS_PRETTY_TRACE_DEBUG("Rights violation during DirectoryOpen");
    return write_error(std::move(channel), status);
  }
  OpenAt(vfs_, vnode_, std::move(channel), fbl::StringPiece(path_data, path_size), open_options,
         options_.rights, mode);
  return ZX_OK;
}

zx_status_t Connection::DirectoryUnlink(const char* path_data, size_t path_size, fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryUnlink] our options: ", options_,
                        ", path: ", Path(path_data, path_size));

  if (options_.flags.node_reference) {
    return fuchsia_io_DirectoryUnlink_reply(txn, ZX_ERR_BAD_HANDLE);
  }
  if (!options_.rights.write) {
    return fuchsia_io_DirectoryUnlink_reply(txn, ZX_ERR_BAD_HANDLE);
  }
  zx_status_t status = vfs_->Unlink(vnode_, fbl::StringPiece(path_data, path_size));
  return fuchsia_io_DirectoryUnlink_reply(txn, status);
}

zx_status_t Connection::DirectoryReadDirents(uint64_t max_out, fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryReadDirents] our options: ", options_);

  if (options_.flags.node_reference) {
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
  FS_PRETTY_TRACE_DEBUG("[DirectoryRewind] our options: ", options_);

  if (options_.flags.node_reference) {
    return fuchsia_io_DirectoryRewind_reply(txn, ZX_ERR_BAD_HANDLE);
  }
  dircookie_.Reset();
  return fuchsia_io_DirectoryRewind_reply(txn, ZX_OK);
}

zx_status_t Connection::DirectoryGetToken(fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryGetToken] our options: ", options_);

  if (!options_.rights.write) {
    return fuchsia_io_DirectoryGetToken_reply(txn, ZX_ERR_BAD_HANDLE, ZX_HANDLE_INVALID);
  }
  zx::event returned_token;
  zx_status_t status = vfs_->VnodeToToken(vnode_, &token_, &returned_token);
  return fuchsia_io_DirectoryGetToken_reply(txn, status, returned_token.release());
}

zx_status_t Connection::DirectoryRename(const char* src_data, size_t src_size,
                                        zx_handle_t dst_parent_token, const char* dst_data,
                                        size_t dst_size, fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryRename] our options: ", options_,
                        ", src: ", Path(src_data, src_size), ", dst: ", Path(dst_data, dst_size));

  zx::event token(dst_parent_token);
  fbl::StringPiece oldStr(src_data, src_size);
  fbl::StringPiece newStr(dst_data, dst_size);

  if (src_size < 1 || dst_size < 1) {
    return fuchsia_io_DirectoryRename_reply(txn, ZX_ERR_INVALID_ARGS);
  }
  if (options_.flags.node_reference) {
    return fuchsia_io_DirectoryRename_reply(txn, ZX_ERR_BAD_HANDLE);
  }
  if (!options_.rights.write) {
    return fuchsia_io_DirectoryRename_reply(txn, ZX_ERR_BAD_HANDLE);
  }
  zx_status_t status = vfs_->Rename(std::move(token), vnode_, std::move(oldStr), std::move(newStr));
  return fuchsia_io_DirectoryRename_reply(txn, status);
}

zx_status_t Connection::DirectoryLink(const char* src_data, size_t src_size,
                                      zx_handle_t dst_parent_token, const char* dst_data,
                                      size_t dst_size, fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryLink] our options: ", options_,
                        ", src: ", Path(src_data, src_size), ", dst: ", Path(dst_data, dst_size));

  zx::event token(dst_parent_token);
  fbl::StringPiece oldStr(src_data, src_size);
  fbl::StringPiece newStr(dst_data, dst_size);

  if (src_size < 1 || dst_size < 1) {
    return fuchsia_io_DirectoryLink_reply(txn, ZX_ERR_INVALID_ARGS);
  }
  if (options_.flags.node_reference) {
    return fuchsia_io_DirectoryLink_reply(txn, ZX_ERR_BAD_HANDLE);
  }
  if (!options_.rights.write) {
    return fuchsia_io_DirectoryLink_reply(txn, ZX_ERR_BAD_HANDLE);
  }
  zx_status_t status = vfs_->Link(std::move(token), vnode_, std::move(oldStr), std::move(newStr));
  return fuchsia_io_DirectoryLink_reply(txn, status);
}

zx_status_t Connection::DirectoryWatch(uint32_t mask, uint32_t options, zx_handle_t handle,
                                       fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryWatch] our options: ", options_);

  if (options_.flags.node_reference) {
    return fuchsia_io_DirectoryWatch_reply(txn, ZX_ERR_BAD_HANDLE);
  }
  zx::channel watcher(handle);
  zx_status_t status = vnode_->WatchDir(vfs_, mask, options, std::move(watcher));
  return fuchsia_io_DirectoryWatch_reply(txn, status);
}

zx_status_t Connection::DirectoryAdminMount(zx_handle_t remote, fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryAdminMount] our options: ", options_);

  if (!options_.rights.admin) {
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
  FS_PRETTY_TRACE_DEBUG("[DirectoryAdminMountAndCreate] our options: ", options_);

  if (!options_.rights.admin) {
    vfs_unmount_handle(remote, 0);
    return fuchsia_io_DirectoryAdminMount_reply(txn, ZX_ERR_ACCESS_DENIED);
  }
  fbl::StringPiece str(name, name_size);
  zx_status_t status = vfs_->MountMkdir(vnode_, std::move(str), MountChannel(remote), flags);
  return fuchsia_io_DirectoryAdminMount_reply(txn, status);
}

zx_status_t Connection::DirectoryAdminUnmount(fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryAdminUnmount] our options: ", options_);

  if (!options_.rights.admin) {
    return fuchsia_io_DirectoryAdminUnmount_reply(txn, ZX_ERR_ACCESS_DENIED);
  }
  vfs_->UninstallAll(ZX_TIME_INFINITE);

  // Unmount is fatal to the requesting connections.
  Vfs::ShutdownCallback closure(
      [ch = std::move(channel_), ctxn = FidlConnection::CopyTxn(txn)](zx_status_t status) mutable {
        fuchsia_io_DirectoryAdminUnmount_reply(ctxn.Txn(), status);
      });
  Vfs* vfs = vfs_;
  Terminate(/* call_close= */ true);
  vfs->Shutdown(std::move(closure));
  return ERR_DISPATCHER_ASYNC;
}

zx_status_t Connection::DirectoryAdminUnmountNode(fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryAdminUnmountNode] our options: ", options_);

  if (!options_.rights.admin) {
    return fuchsia_io_DirectoryAdminUnmountNode_reply(txn, ZX_ERR_ACCESS_DENIED, ZX_HANDLE_INVALID);
  }
  zx::channel c;
  zx_status_t status = vfs_->UninstallRemote(vnode_, &c);
  return fuchsia_io_DirectoryAdminUnmountNode_reply(txn, status, c.release());
}

zx_status_t Connection::DirectoryAdminQueryFilesystem(fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryAdminQueryFilesystem] our options: ", options_);

  fuchsia_io_FilesystemInfo info;
  zx_status_t status = vnode_->QueryFilesystem(&info);
  return fuchsia_io_DirectoryAdminQueryFilesystem_reply(txn, status,
                                                        status == ZX_OK ? &info : nullptr);
}

zx_status_t Connection::DirectoryAdminGetDevicePath(fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryAdminGetDevicePath] our options: ", options_);

  if (!options_.rights.admin) {
    return fuchsia_io_DirectoryAdminGetDevicePath_reply(txn, ZX_ERR_ACCESS_DENIED, nullptr, 0);
  }

  char name[fuchsia_io_MAX_PATH];
  size_t actual = 0;
  zx_status_t status = vnode_->GetDevicePath(sizeof(name), name, &actual);
  return fuchsia_io_DirectoryAdminGetDevicePath_reply(txn, status, name, actual);
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
  return vnode_->HandleFsSpecificMessage(msg, txn);
}

}  // namespace internal

}  // namespace fs
