// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/connection.h"

#include <fcntl.h>
#include <fidl/fuchsia.hardware.pty/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/io.h>
#include <lib/fdio/vfs.h>
#include <lib/fidl/txn_header.h>
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

#include "src/lib/storage/vfs/cpp/debug.h"
#include "src/lib/storage/vfs/cpp/fidl_transaction.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

namespace fio = fuchsia_io;

static_assert(fio::wire::kOpenFlagsAllowedWithNodeReference ==
                  (fio::wire::OpenFlags::kDirectory | fio::wire::OpenFlags::kNotDirectory |
                   fio::wire::OpenFlags::kDescribe | fio::wire::OpenFlags::kNodeReference),
              "OPEN_FLAGS_ALLOWED_WITH_NODE_REFERENCE value mismatch");
static_assert(PATH_MAX == fio::wire::kMaxPath, "POSIX PATH_MAX inconsistent with Fuchsia MAX_PATH");
static_assert(NAME_MAX == fio::wire::kMaxFilename,
              "POSIX NAME_MAX inconsistent with Fuchsia MAX_FILENAME");

namespace fs {

constexpr zx_signals_t kWakeSignals =
    ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED | kLocalTeardownSignal;

namespace internal {

zx::result<VnodeRepresentation> Describe(const fbl::RefPtr<Vnode>& vnode, VnodeProtocol protocol,
                                         VnodeConnectionOptions options) {
  if (options.flags.node_reference) {
    return zx::ok(VnodeRepresentation::Connector());
  }
  fs::VnodeRepresentation representation;
  zx_status_t status = vnode->GetNodeInfoForProtocol(protocol, options.rights, &representation);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(representation));
}

bool PrevalidateFlags(fio::wire::OpenFlags flags) {
  if (flags & fio::wire::OpenFlags::kNodeReference) {
    // Explicitly reject VNODE_REF_ONLY together with any invalid flags.
    if (flags & ~fio::wire::kOpenFlagsAllowedWithNodeReference) {
      return false;
    }
  }

  if ((flags & fio::wire::OpenFlags::kNotDirectory) && (flags & fio::wire::OpenFlags::kDirectory)) {
    return false;
  }

  return true;
}

zx_status_t EnforceHierarchicalRights(Rights parent_rights, VnodeConnectionOptions child_options,
                                      VnodeConnectionOptions* out_options) {
  // The POSIX compatibiltiy flags allow the child directory connection to inherit the writable
  // and executable rights.  If there exists a directory without the corresponding right along
  // the Open() chain, we remove that POSIX flag preventing it from being inherited down the line
  // (this applies both for local and remote mount points, as the latter may be served using
  // a connection with vastly greater rights).
  if (child_options.flags.posix_write && !parent_rights.write) {
    child_options.flags.posix_write = false;
  }
  if (child_options.flags.posix_execute && !parent_rights.execute) {
    child_options.flags.posix_execute = false;
  }
  if (!child_options.rights.StricterOrSameAs(parent_rights)) {
    // Client asked for some right but we do not have it
    return ZX_ERR_ACCESS_DENIED;
  }
  *out_options = child_options;
  return ZX_OK;
}

Binding::Binding(Connection& connection, async_dispatcher_t* dispatcher, zx::channel channel)
    : wait_(this, channel.get(), kWakeSignals, 0),
      connection_(connection),
      dispatcher_(dispatcher),
      channel_(std::move(channel)) {}

Binding::~Binding() { CancelDispatching(); }

Connection::Connection(FuchsiaVfs* vfs, fbl::RefPtr<Vnode> vnode, VnodeProtocol protocol,
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

  // Release the token associated with this connection's vnode since the connection will be
  // releasing the vnode's reference once this function returns.
  if (token_) {
    vfs_->TokenDiscard(std::move(token_));
  }
}

void Connection::AsyncTeardown() {
  OnTeardown();
  if (std::shared_ptr<Binding> binding = binding_; binding) {
    binding->AsyncTeardown();
  }
}

void Binding::AsyncTeardown() {
  // This will wake up the dispatcher to call |Binding::HandleSignals| and eventually result in
  // |Connection::SyncTeardown|.
  ZX_ASSERT(channel_.signal(0, kLocalTeardownSignal) == ZX_OK);
}

zx_status_t Connection::StartDispatching(zx::channel channel) {
  ZX_DEBUG_ASSERT(channel);
  ZX_DEBUG_ASSERT(!binding_);
  ZX_DEBUG_ASSERT(vfs_->dispatcher());
  ZX_DEBUG_ASSERT_MSG(InContainer(),
                      "Connection must be managed by the Vfs when dispatching FIDL messages.");

  binding_ = std::make_shared<Binding>(*this, vfs_->dispatcher(), std::move(channel));
  zx_status_t status = binding_->StartDispatching();
  if (status != ZX_OK) {
    binding_.reset();
    return status;
  }
  return ZX_OK;
}

zx_status_t Binding::StartDispatching() {
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

void Binding::HandleSignals(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                            zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK || !(signal->observed & ZX_CHANNEL_READABLE)) {
    connection_.SyncTeardown();
    return;
  }
  bool handling_ok = connection_.OnMessage();
  if (!handling_ok) {
    connection_.SyncTeardown();
  }
}

bool Connection::OnMessage() {
  if (vfs_->IsTerminating()) {
    // Short-circuit locally destroyed connections, rather than servicing requests on their behalf.
    // This prevents new requests from being served while filesystems are torn down.
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
  fidl_channel_handle_metadata_t handle_metadata[ZX_CHANNEL_MAX_MSG_HANDLES];
  fidl::IncomingHeaderAndMessage msg =
      fidl::MessageRead(binding->channel(), fidl::ChannelMessageStorageView{
                                                .bytes = fidl::BufferSpan(bytes, sizeof(bytes)),
                                                .handles = handles,
                                                .handle_metadata = handle_metadata,
                                                .handle_capacity = ZX_CHANNEL_MAX_MSG_HANDLES,
                                            });
  if (!msg.ok()) {
    return false;
  }

  auto* header = msg.header();
  FidlTransaction txn(header->txid, binding);

  ::fidl::DispatchResult dispatch_result = fidl_protocol_.TryDispatch(msg, &txn);
  if (dispatch_result == ::fidl::DispatchResult::kNotFound) {
    vnode_->HandleFsSpecificMessage(msg, &txn);
  }

  switch (txn.ToResult()) {
    case FidlTransaction::Result::kRepliedSynchronously:
      // If we get here, the message was successfully handled, synchronously.
      return binding->StartDispatching() == ZX_OK;
    case FidlTransaction::Result::kPendingAsyncReply:
      // If we get here, the transaction was converted to an async one. Dispatching will be resumed
      // by the transaction when it is completed.
      return true;
    case FidlTransaction::Result::kClosed:
      return false;
  }
#ifdef __GNUC__
  // GCC does not infer that the above switch statement will always return by handling all defined
  // enum members.
  __builtin_abort();
#endif
}

void Connection::SyncTeardown() {
  OnTeardown();
  EnsureVnodeClosed();
  binding_.reset();

  // Tell the VFS that the connection closed remotely. This might have the side-effect of destroying
  // this object, so this must be the last statement.
  vfs_->OnConnectionClosedRemotely(this);
}

zx_status_t Connection::EnsureVnodeClosed() {
  if (!vnode_is_open_) {
    return ZX_OK;
  }
  vnode_is_open_ = false;
  return vnode_->Close();
}

void Connection::NodeClone(fio::wire::OpenFlags flags, fidl::ServerEnd<fio::Node> server_end) {
  auto clone_options = VnodeConnectionOptions::FromIoV1Flags(flags);
  auto write_error = [describe = clone_options.flags.describe](fidl::ServerEnd<fio::Node> channel,
                                                               zx_status_t error) {
    if (describe) {
      // Ignore errors since there is nothing we can do if this fails.
      [[maybe_unused]] auto result =
          fidl::WireSendEvent(channel)->OnOpen(error, fio::wire::NodeInfoDeprecated());
      channel.reset();
    }
  };
  if (!PrevalidateFlags(flags)) {
    FS_PRETTY_TRACE_DEBUG("[NodeClone] prevalidate failed", ", incoming flags: ", flags);
    return write_error(std::move(server_end), ZX_ERR_INVALID_ARGS);
  }
  FS_PRETTY_TRACE_DEBUG("[NodeClone] our options: ", options(),
                        ", incoming options: ", clone_options);

  // If CLONE_SAME_RIGHTS is specified, the client cannot request any specific rights.
  if (clone_options.flags.clone_same_rights && clone_options.rights.any()) {
    return write_error(std::move(server_end), ZX_ERR_INVALID_ARGS);
  }
  // These two flags are always preserved.
  clone_options.flags.append = options().flags.append;
  clone_options.flags.node_reference = options().flags.node_reference;
  // If CLONE_SAME_RIGHTS is requested, cloned connection will inherit the same rights as those from
  // the originating connection.
  if (clone_options.flags.clone_same_rights) {
    clone_options.rights = options().rights;
  }
  if (!vnode()->IsSkipRightsEnforcementDevfsOnlyDoNotUse()) {
    if (!clone_options.rights.StricterOrSameAs(options().rights)) {
      FS_PRETTY_TRACE_DEBUG("Rights violation during NodeClone");
      return write_error(std::move(server_end), ZX_ERR_ACCESS_DENIED);
    }
  }

  fbl::RefPtr<Vnode> vn(vnode_);
  auto result = vn->ValidateOptions(clone_options);
  if (result.is_error()) {
    return write_error(std::move(server_end), result.status_value());
  }
  auto& validated_options = result.value();
  zx_status_t open_status = ZX_OK;
  if (!clone_options.flags.node_reference) {
    open_status = OpenVnode(validated_options, &vn);
  }
  if (open_status != ZX_OK) {
    return write_error(std::move(server_end), open_status);
  }

  vfs_->Serve(vn, server_end.TakeChannel(), validated_options);
}

zx::result<> Connection::NodeClose() {
  zx::result result = zx::make_result(EnsureVnodeClosed());
  closing_ = true;
  AsyncTeardown();
  return result;
}

fidl::VectorView<uint8_t> Connection::NodeQuery() {
  const std::string_view kProtocol = [this]() {
    if (options().flags.node_reference) {
      return fio::wire::kNodeProtocolName;
    }
    switch (protocol()) {
      case VnodeProtocol::kConnector: {
        return fio::wire::kNodeProtocolName;
      }
      case VnodeProtocol::kFile: {
        return fio::wire::kFileProtocolName;
      }
      case VnodeProtocol::kDirectory: {
        return fio::wire::kDirectoryProtocolName;
      }
      case VnodeProtocol::kTty: {
        return fuchsia_hardware_pty::wire::kDeviceProtocolName;
      }
    }
  }();
  uint8_t* data = reinterpret_cast<uint8_t*>(const_cast<char*>(kProtocol.data()));
  return fidl::VectorView<uint8_t>::FromExternal(data, kProtocol.size());
}

zx::result<VnodeRepresentation> Connection::NodeDescribe() {
  return Describe(vnode(), protocol(), options());
}

void Connection::NodeSync(fit::callback<void(zx_status_t)> callback) {
  FS_PRETTY_TRACE_DEBUG("[NodeSync] options: ", options());

  if (options().flags.node_reference) {
    return callback(ZX_ERR_BAD_HANDLE);
  }
  vnode_->Sync(Vnode::SyncCallback(std::move(callback)));
}

zx::result<VnodeAttributes> Connection::NodeGetAttr() {
  FS_PRETTY_TRACE_DEBUG("[NodeGetAttr] options: ", options());

  fs::VnodeAttributes attr;
  if (zx_status_t status = vnode_->GetAttributes(&attr); status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(attr);
}

zx::result<> Connection::NodeSetAttr(fio::wire::NodeAttributeFlags flags,
                                     const fio::wire::NodeAttributes& attributes) {
  FS_PRETTY_TRACE_DEBUG("[NodeSetAttr] our options: ", options(), ", incoming flags: ", flags);

  if (options().flags.node_reference) {
    return zx::error(ZX_ERR_BAD_HANDLE);
  }
  if (!options().rights.write) {
    return zx::error(ZX_ERR_BAD_HANDLE);
  }

  fs::VnodeAttributesUpdate update;
  if (flags & fio::wire::NodeAttributeFlags::kCreationTime) {
    update.set_creation_time(attributes.creation_time);
  }
  if (flags & fio::wire::NodeAttributeFlags::kModificationTime) {
    update.set_modification_time(attributes.modification_time);
  }
  return zx::make_result(vnode_->SetAttributes(update));
}

zx::result<fio::wire::OpenFlags> Connection::NodeGetFlags() {
  return zx::ok(options().ToIoV1Flags() & (kStatusFlags | fio::wire::kOpenRights));
}

zx::result<> Connection::NodeSetFlags(fio::wire::OpenFlags flags) {
  auto options = VnodeConnectionOptions::FromIoV1Flags(flags);
  set_append(options.flags.append);
  return zx::ok();
}

zx_koid_t Connection::GetChannelOwnerKoid() {
  if (binding_ == nullptr) {
    return ZX_KOID_INVALID;
  }
  auto& channel = binding_->channel();

  if (!channel.is_valid()) {
    return ZX_KOID_INVALID;
  }

  zx_info_handle_basic_t owner_info;
  if (zx_object_get_info(channel.get(), ZX_INFO_HANDLE_BASIC, &owner_info, sizeof(owner_info),
                         nullptr, nullptr) != ZX_OK) {
    return ZX_KOID_INVALID;
  }

  return owner_info.koid;
}

zx::result<fuchsia_io::wire::FilesystemInfo> Connection::NodeQueryFilesystem() {
  zx::result<FilesystemInfo> info = vfs_->GetFilesystemInfo();
  if (info.is_error()) {
    return info.take_error();
  }
  return zx::ok(info.value().ToFidl());
}

}  // namespace internal

}  // namespace fs
