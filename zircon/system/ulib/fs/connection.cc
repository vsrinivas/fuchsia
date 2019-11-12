// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/internal/connection.h>

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

zx_status_t GetNodeInfo(const fbl::RefPtr<Vnode>& vn, VnodeProtocol protocol,
                        VnodeConnectionOptions options, fuchsia_io_NodeInfo* info) {
  if (options.flags.node_reference) {
    info->tag = fuchsia_io_NodeInfoTag_service;
    return ZX_OK;
  } else {
    fs::VnodeRepresentation representation;
    zx_status_t status = vn->GetNodeInfoForProtocol(protocol, options.rights, &representation);
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

}  // namespace

constexpr zx_signals_t kWakeSignals =
    ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED | kLocalTeardownSignal;

namespace internal {

void Describe(const fbl::RefPtr<Vnode>& vn, VnodeProtocol protocol, VnodeConnectionOptions options,
              OnOpenMsg* response, zx_handle_t* handle) {
  fidl_init_txn_header(&response->primary.hdr, 0, fuchsia_io_NodeOnOpenOrdinal);
  response->extra.file.event = ZX_HANDLE_INVALID;
  zx_status_t r = GetNodeInfo(vn, protocol, options, &response->extra);

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

void WriteDescribeError(zx::channel channel, zx_status_t status) {
  fuchsia_io_NodeOnOpenEvent msg;
  memset(&msg, 0, sizeof(msg));
  fidl_init_txn_header(&msg.hdr, 0, fuchsia_io_NodeOnOpenOrdinal);
  msg.s = status;
  channel.write(0, &msg, sizeof(msg), nullptr, 0);
}

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

Connection::Connection(Vfs* vfs, fbl::RefPtr<Vnode> vnode, zx::channel channel,
                       VnodeProtocol protocol, VnodeConnectionOptions options)
    : vfs_(vfs),
      vnode_(std::move(vnode)),
      channel_(std::move(channel)),
      wait_(this, ZX_HANDLE_INVALID, kWakeSignals),
      protocol_(protocol),
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

zx_status_t Connection::UnmountAndShutdown(fidl_txn_t* txn) {
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

zx_status_t Connection::StartDispatching() {
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
  FS_PRETTY_TRACE_DEBUG("[NodeClone] our options: ", options(),
                        ", incoming options: ", clone_options);

  // If CLONE_SAME_RIGHTS is specified, the client cannot request any specific rights.
  if (clone_options.flags.clone_same_rights && clone_options.rights.any()) {
    return write_error(std::move(channel), ZX_ERR_INVALID_ARGS);
  }
  // These two flags are always preserved.
  clone_options.flags.append = options().flags.append;
  clone_options.flags.node_reference = options().flags.node_reference;
  // If CLONE_SAME_RIGHTS is requested, cloned connection will inherit the same rights
  // as those from the originating connection.
  if (clone_options.flags.clone_same_rights) {
    clone_options.rights = options().rights;
  }
  if (!clone_options.rights.StricterOrSameAs(options().rights)) {
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
  if (open_status != ZX_OK) {
    return write_error(std::move(channel), open_status);
  }

  vfs_->Serve(vn, std::move(channel), validated_options);
  return ZX_OK;
}

zx_status_t Connection::NodeClose(fidl_txn_t* txn) {
  zx_status_t status;
  if (options().flags.node_reference) {
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
  zx_status_t status = GetNodeInfo(vnode_, protocol(), options(), &info);
  if (status != ZX_OK) {
    return status;
  }
  return fuchsia_io_NodeDescribe_reply(txn, &info);
}

zx_status_t Connection::NodeSync(fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[NodeSync] options: ", options());

  if (options().flags.node_reference) {
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
  FS_PRETTY_TRACE_DEBUG("[NodeGetAttr] options: ", options());

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
  FS_PRETTY_TRACE_DEBUG("[NodeSetAttr] our options: ", options(), ", incoming flags: ", flags);

  if (options().flags.node_reference) {
    return fuchsia_io_NodeSetAttr_reply(txn, ZX_ERR_BAD_HANDLE);
  }
  if (!options().rights.write) {
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
  uint32_t flags = options().ToIoV1Flags() & (kStatusFlags | ZX_FS_RIGHTS);
  return fuchsia_io_NodeNodeGetFlags_reply(txn, ZX_OK, flags);
}

zx_status_t Connection::NodeNodeSetFlags(uint32_t flags, fidl_txn_t* txn) {
  auto options = VnodeConnectionOptions::FromIoV1Flags(flags);
  set_append(options.flags.append);
  return fuchsia_io_NodeNodeSetFlags_reply(txn, ZX_OK);
}

}  // namespace internal

}  // namespace fs
