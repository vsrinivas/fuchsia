// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/io/llcpp/fidl.h>
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

#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

#include <fbl/string_buffer.h>
#include <fs/debug.h>
#include <fs/internal/connection.h>
#include <fs/internal/fidl_transaction.h>
#include <fs/trace.h>
#include <fs/vfs_types.h>
#include <fs/vnode.h>

namespace fio = ::llcpp::fuchsia::io;

static_assert(fio::OPEN_FLAGS_ALLOWED_WITH_NODE_REFERENCE ==
                  (fio::OPEN_FLAG_DIRECTORY | fio::OPEN_FLAG_NOT_DIRECTORY |
                   fio::OPEN_FLAG_DESCRIBE | fio::OPEN_FLAG_NODE_REFERENCE),
              "OPEN_FLAGS_ALLOWED_WITH_NODE_REFERENCE value mismatch");
static_assert(PATH_MAX == fio::MAX_PATH, "POSIX PATH_MAX inconsistent with Fuchsia MAX_PATH");
static_assert(NAME_MAX == fio::MAX_FILENAME,
              "POSIX NAME_MAX inconsistent with Fuchsia MAX_FILENAME");

namespace fs {

constexpr zx_signals_t kWakeSignals =
    ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED | kLocalTeardownSignal;

namespace internal {

fit::result<VnodeRepresentation, zx_status_t> Describe(const fbl::RefPtr<Vnode>& vnode,
                                                       VnodeProtocol protocol,
                                                       VnodeConnectionOptions options) {
  if (options.flags.node_reference) {
    return fit::ok(VnodeRepresentation::Connector());
  }
  fs::VnodeRepresentation representation;
  zx_status_t status = vnode->GetNodeInfoForProtocol(protocol, options.rights, &representation);
  if (status != ZX_OK) {
    return fit::error(status);
  }
  return fit::ok(std::move(representation));
}

bool PrevalidateFlags(uint32_t flags) {
  // If the caller specified an unknown right, reject the request.
  if ((flags & ZX_FS_RIGHTS_SPACE) & ~ZX_FS_RIGHTS) {
    return false;
  }

  if (flags & fio::OPEN_FLAG_NODE_REFERENCE) {
    constexpr uint32_t kValidFlagsForNodeRef =
        fio::OPEN_FLAG_NODE_REFERENCE | fio::OPEN_FLAG_DIRECTORY | fio::OPEN_FLAG_NOT_DIRECTORY |
        fio::OPEN_FLAG_DESCRIBE;
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

Binding::Binding(Connection* connection, async_dispatcher_t* dispatcher, zx::channel channel)
    : wait_(this, channel.get(), kWakeSignals, 0),
      connection_(connection),
      dispatcher_(dispatcher),
      channel_(std::move(channel)) {}

Binding::~Binding() { CancelDispatching(); }

Connection::Connection(Vfs* vfs, fbl::RefPtr<Vnode> vnode, VnodeProtocol protocol,
                       VnodeConnectionOptions options, FidlProtocol fidl_protocol)
    : vnode_is_open_(!options.flags.node_reference),
      vfs_(vfs),
      vnode_(std::move(vnode)),
      protocol_(protocol),
      options_(VnodeConnectionOptions::FilterForNewConnection(options)),
      fidl_protocol_(fidl_protocol) {
  ZX_DEBUG_ASSERT(vfs);
  ZX_DEBUG_ASSERT(vnode_);
}

Connection::~Connection() {
  // Invoke a "close" call on the underlying vnode if we haven't already.
  EnsureVnodeClosed();

  // Release the token associated with this connection's vnode since the connection
  // will be releasing the vnode's reference once this function returns.
  if (token_) {
    vfs_->TokenDiscard(std::move(token_));
  }
}

void Connection::AsyncTeardown() {
  if (std::shared_ptr<Binding> binding = binding_; binding) {
    binding->AsyncTeardown();
  }
}

void Binding::AsyncTeardown() {
  // This will wake up the dispatcher to call |Binding::HandleSignals|
  // and eventually result in |Connection::SyncTeardown|.
  ZX_ASSERT(channel_.signal(0, kLocalTeardownSignal) == ZX_OK);
}

void Connection::UnmountAndShutdown(fit::callback<void(zx_status_t)> callback) {
  vfs_->UninstallAll(zx::time::infinite());
  // We need the binding to live on in order to make a reply to this FIDL request.
  // However, the connection object may be destroyed before the binding. We need to
  // stop the binding from monitoring further incoming FIDL messages.
  binding_->DetachFromConnection();
  Vfs::ShutdownCallback closure([binding = std::move(binding_), callback = std::move(callback)](
                                    zx_status_t status) mutable { callback(status); });
  Vfs* vfs = vfs_;
  SyncTeardown();
  vfs->Shutdown(std::move(closure));
}

zx_status_t Connection::StartDispatching(zx::channel channel) {
  ZX_DEBUG_ASSERT(channel);
  ZX_DEBUG_ASSERT(!binding_);
  ZX_DEBUG_ASSERT(vfs_->dispatcher());
  ZX_DEBUG_ASSERT_MSG(InContainer(),
                      "Connection must be managed by the Vfs when dispatching FIDL messages.");

  binding_ = std::make_shared<Binding>(this, vfs_->dispatcher(), std::move(channel));
  zx_status_t status = binding_->StartDispatching();
  if (status != ZX_OK) {
    binding_.reset();
    return status;
  }
  return ZX_OK;
}

zx_status_t Binding::StartDispatching() {
  if (!connection_) {
    return ZX_OK;
  }
  ZX_DEBUG_ASSERT(!wait_.is_pending());
  return wait_.Begin(dispatcher_);
}

void Binding::CancelDispatching() {
  // Stop waiting and clean up if still connected.
  if (wait_.is_pending()) {
    zx_status_t status = wait_.Cancel();
    ZX_DEBUG_ASSERT_MSG(status == ZX_OK, "Could not cancel wait: status=%d", status);
  }
}

void Binding::DetachFromConnection() {
  CancelDispatching();
  UnregisterInflightTransaction();
  connection_ = nullptr;
}

void Binding::HandleSignals(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                            zx_status_t status, const zx_packet_signal_t* signal) {
  if (!connection_) {
    // Before a |Connection| is destructed, it will clear this pointer
    // in its corresponding |Binding| by calling |DetachFromConnection|.
    return;
  }
  if (status != ZX_OK || !(signal->observed & ZX_CHANNEL_READABLE)) {
    connection_->SyncTeardown();
    return;
  }
  bool handling_ok = connection_->OnMessage();
  if (!handling_ok) {
    connection_->SyncTeardown();
  }
}

bool Connection::OnMessage() {
  if (vfs_->IsTerminating()) {
    // Short-circuit locally destroyed connections, rather than servicing
    // requests on their behalf. This prevents new requests from being
    // served while filesystems are torn down.
    return false;
  }
  if (closing_) {
    // This prevents subsequent requests from being served after the
    // observation of a |Node.Close| call.
    return false;
  }
  std::shared_ptr<Binding> binding = binding_;
  uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  fidl_incoming_msg_t msg = {
      .bytes = bytes,
      .handles = handles,
      .num_bytes = 0,
      .num_handles = 0,
  };
  zx_status_t r = binding->channel().read(0, bytes, handles, std::size(bytes), std::size(handles),
                                          &msg.num_bytes, &msg.num_handles);
  if (r != ZX_OK) {
    return false;
  }

  // Do basic validation on the message.
  auto* header = reinterpret_cast<fidl_message_header_t*>(msg.bytes);
  r = msg.num_bytes < sizeof(fidl_message_header_t) ? ZX_ERR_INVALID_ARGS
                                                    : fidl_validate_txn_header(header);
  if (r != ZX_OK) {
    zx_handle_close_many(msg.handles, msg.num_handles);
    return false;
  }

  FidlTransaction txn(header->txid, binding);

  ::fidl::DispatchResult dispatch_result = fidl_protocol_.TryDispatch(&msg, &txn);
  if (dispatch_result == ::fidl::DispatchResult::kNotFound) {
    vnode_->HandleFsSpecificMessage(&msg, &txn);
  }

  switch (txn.ToResult()) {
    case FidlTransaction::Result::kRepliedSynchronously:
      // If we get here, the message was successfully handled, synchronously.
      return binding->StartDispatching() == ZX_OK;
    case FidlTransaction::Result::kPendingAsyncReply:
      // If we get here, the transaction was converted to an async one.
      // Dispatching will be resumed by the transaction when it is completed.
      return true;
    case FidlTransaction::Result::kClosed:
      return false;
  }
#ifdef __GNUC__
  // GCC does not infer that the above switch statement will always return by
  // handling all defined enum members.
  __builtin_abort();
#endif
}

void Connection::SyncTeardown() {
  EnsureVnodeClosed();
  binding_.reset();

  // Tell the VFS that the connection closed remotely.
  // This might have the side-effect of destroying this object,
  // so this must be the last statement.
  vfs_->OnConnectionClosedRemotely(this);
}

zx_status_t Connection::EnsureVnodeClosed() {
  if (!vnode_is_open_) {
    return ZX_OK;
  }
  vnode_is_open_ = false;
  return vnode_->Close();
}

void Connection::NodeClone(uint32_t clone_flags, zx::channel channel) {
  auto clone_options = VnodeConnectionOptions::FromIoV1Flags(clone_flags);
  auto write_error = [describe = clone_options.flags.describe](zx::channel channel,
                                                               zx_status_t error) {
    if (describe) {
      fio::Node::SendOnOpenEvent(zx::unowned_channel(channel), error, fio::NodeInfo());
    }
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
}

Connection::Result<> Connection::NodeClose() {
  Result<> result = FromStatus(EnsureVnodeClosed());
  closing_ = true;
  AsyncTeardown();
  return result;
}

Connection::Result<VnodeRepresentation> Connection::NodeDescribe() {
  return Describe(vnode(), protocol(), options());
}

void Connection::NodeSync(fit::callback<void(zx_status_t)> callback) {
  FS_PRETTY_TRACE_DEBUG("[NodeSync] options: ", options());

  if (options().flags.node_reference) {
    return callback(ZX_ERR_BAD_HANDLE);
  }
  vnode_->Sync(Vnode::SyncCallback(std::move(callback)));
}

Connection::Result<VnodeAttributes> Connection::NodeGetAttr() {
  FS_PRETTY_TRACE_DEBUG("[NodeGetAttr] options: ", options());

  fs::VnodeAttributes attr;
  zx_status_t r;
  if ((r = vnode_->GetAttributes(&attr)) != ZX_OK) {
    return fit::error(r);
  }
  return fit::ok(attr);
}

Connection::Result<> Connection::NodeSetAttr(uint32_t flags,
                                             const fio::NodeAttributes& attributes) {
  FS_PRETTY_TRACE_DEBUG("[NodeSetAttr] our options: ", options(), ", incoming flags: ", flags);

  if (options().flags.node_reference) {
    return fit::error(ZX_ERR_BAD_HANDLE);
  }
  if (!options().rights.write) {
    return fit::error(ZX_ERR_BAD_HANDLE);
  }
  constexpr uint32_t supported_flags =
      fio::NODE_ATTRIBUTE_FLAG_CREATION_TIME | fio::NODE_ATTRIBUTE_FLAG_MODIFICATION_TIME;
  if (flags & ~supported_flags) {
    return fit::error(ZX_ERR_INVALID_ARGS);
  }

  zx_status_t status = vnode_->SetAttributes(
      fs::VnodeAttributesUpdate()
          .set_creation_time(flags & fio::NODE_ATTRIBUTE_FLAG_CREATION_TIME
                                 ? std::make_optional(attributes.creation_time)
                                 : std::nullopt)
          .set_modification_time(flags & fio::NODE_ATTRIBUTE_FLAG_MODIFICATION_TIME
                                     ? std::make_optional(attributes.modification_time)
                                     : std::nullopt));
  return FromStatus(status);
}

Connection::Result<uint32_t> Connection::NodeNodeGetFlags() {
  return fit::ok(options().ToIoV1Flags() & (kStatusFlags | ZX_FS_RIGHTS));
}

Connection::Result<> Connection::NodeNodeSetFlags(uint32_t flags) {
  auto options = VnodeConnectionOptions::FromIoV1Flags(flags);
  set_append(options.flags.append);
  return fit::ok();
}

}  // namespace internal

}  // namespace fs
