// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/fuchsia_vfs.h"

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/watcher.h>
#include <lib/zx/event.h>
#include <lib/zx/process.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/assert.h>

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>

#include "src/lib/storage/vfs/cpp/connection.h"
#include "src/lib/storage/vfs/cpp/debug.h"
#include "src/lib/storage/vfs/cpp/directory_connection.h"
#include "src/lib/storage/vfs/cpp/node_connection.h"
#include "src/lib/storage/vfs/cpp/remote_file_connection.h"
#include "src/lib/storage/vfs/cpp/stream_file_connection.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

namespace fio = fuchsia_io;

namespace fs {
namespace {

zx_koid_t GetTokenKoid(const zx::event& token) {
  zx_info_handle_basic_t info = {};
  token.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  return info.koid;
}

uint32_t ToStreamOptions(const VnodeConnectionOptions& options) {
  uint32_t stream_options = 0u;
  if (options.rights.read) {
    stream_options |= ZX_STREAM_MODE_READ;
  }
  if (options.rights.write) {
    stream_options |= ZX_STREAM_MODE_WRITE;
  }
  if (options.flags.append) {
    stream_options |= ZX_STREAM_MODE_APPEND;
  }
  return stream_options;
}

}  // namespace

void FilesystemInfo::SetFsId(const zx::event& event) {
  zx_info_handle_basic_t handle_info;
  if (zx_status_t status =
          event.get_info(ZX_INFO_HANDLE_BASIC, &handle_info, sizeof(handle_info), nullptr, nullptr);
      status == ZX_OK) {
    fs_id = handle_info.koid;
  } else {
    fs_id = ZX_KOID_INVALID;
  }
}

fuchsia_io::wire::FilesystemInfo FilesystemInfo::ToFidl() const {
  fuchsia_io::wire::FilesystemInfo out = {};

  out.total_bytes = total_bytes;
  out.used_bytes = used_bytes;
  out.total_nodes = total_nodes;
  out.used_nodes = used_nodes;
  out.free_shared_pool_bytes = free_shared_pool_bytes;
  out.fs_id = fs_id;
  out.block_size = block_size;
  out.max_filename_size = max_filename_size;
  out.fs_type = fs_type;

  ZX_DEBUG_ASSERT(name.size() < fuchsia_io::wire::kMaxFsNameBuffer);
  out.name[name.copy(reinterpret_cast<char*>(out.name.data()),
                     fuchsia_io::wire::kMaxFsNameBuffer - 1)] = '\0';

  return out;
}

FuchsiaVfs::FuchsiaVfs(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

FuchsiaVfs::~FuchsiaVfs() = default;

void FuchsiaVfs::SetDispatcher(async_dispatcher_t* dispatcher) {
  ZX_ASSERT_MSG(!dispatcher_,
                "FuchsiaVfs::SetDispatcher maybe only be called when dispatcher_ is not set.");
  dispatcher_ = dispatcher;
}

zx_status_t FuchsiaVfs::Unlink(fbl::RefPtr<Vnode> vndir, std::string_view name, bool must_be_dir) {
  if (zx_status_t s = Vfs::Unlink(vndir, name, must_be_dir); s != ZX_OK)
    return s;

  vndir->Notify(name, fio::wire::WatchEvent::kRemoved);
  return ZX_OK;
}

void FuchsiaVfs::TokenDiscard(zx::event ios_token) {
  std::lock_guard lock(vfs_lock_);
  if (ios_token) {
    // The token is cleared here to prevent the following race condition:
    // 1) Open
    // 2) GetToken
    // 3) Close + Release Vnode
    // 4) Use token handle to access defunct vnode (or a different vnode, if the memory for it is
    //    reallocated).
    //
    // By cleared the token cookie, any remaining handles to the event will be ignored by the
    // filesystem server.
    auto rename_request = vnode_tokens_.erase(GetTokenKoid(ios_token));
  }
}

zx_status_t FuchsiaVfs::VnodeToToken(fbl::RefPtr<Vnode> vn, zx::event* ios_token, zx::event* out) {
  std::lock_guard lock(vfs_lock_);
  if (ios_token->is_valid()) {
    // Token has already been set for this iostate
    if (zx_status_t status = ios_token->duplicate(ZX_RIGHTS_BASIC, out); status != ZX_OK) {
      return status;
    }
    return ZX_OK;
  }

  zx::event new_token;
  zx::event new_ios_token;
  if (zx_status_t status = zx::event::create(0, &new_ios_token); status != ZX_OK) {
    return status;
  }
  if (zx_status_t status = new_ios_token.duplicate(ZX_RIGHTS_BASIC, &new_token); status != ZX_OK) {
    return status;
  }
  auto koid = GetTokenKoid(new_ios_token);
  vnode_tokens_.insert(std::make_unique<VnodeToken>(koid, std::move(vn)));
  *ios_token = std::move(new_ios_token);
  *out = std::move(new_token);
  return ZX_OK;
}

bool FuchsiaVfs::IsTokenAssociatedWithVnode(zx::event token) {
  std::lock_guard lock(vfs_lock_);
  return TokenToVnode(std::move(token), nullptr) == ZX_OK;
}

zx::status<bool> FuchsiaVfs::EnsureExists(fbl::RefPtr<Vnode> vndir, std::string_view path,
                                          fbl::RefPtr<Vnode>* out_vn,
                                          fs::VnodeConnectionOptions options, uint32_t mode,
                                          Rights parent_rights) {
  zx::status result = Vfs::EnsureExists(vndir, path, out_vn, options, mode, parent_rights);
  if (result.is_ok()) {
    vndir->Notify(path, fio::wire::WatchEvent::kAdded);
  }
  return result;
}

zx_status_t FuchsiaVfs::TokenToVnode(zx::event token, fbl::RefPtr<Vnode>* out) {
  const auto& vnode_token = vnode_tokens_.find(GetTokenKoid(token));
  if (vnode_token == vnode_tokens_.end()) {
    // TODO(smklein): Return a more specific error code for "token not from this server"
    return ZX_ERR_INVALID_ARGS;
  }

  if (out) {
    *out = vnode_token->get_vnode();
  }
  return ZX_OK;
}

zx_status_t FuchsiaVfs::Rename(zx::event token, fbl::RefPtr<Vnode> oldparent,
                               std::string_view oldStr, std::string_view newStr) {
  // Local filesystem
  bool old_must_be_dir;
  {
    zx::status result = TrimName(oldStr);
    if (result.is_error()) {
      return result.status_value();
    }
    old_must_be_dir = result.value();
    if (oldStr == ".") {
      return ZX_ERR_UNAVAILABLE;
    }
    if (oldStr == "..") {
      return ZX_ERR_INVALID_ARGS;
    }
  }
  bool new_must_be_dir;
  {
    zx::status result = TrimName(newStr);
    if (result.is_error()) {
      return result.status_value();
    }
    new_must_be_dir = result.value();
    if (newStr == "." || newStr == "..") {
      return ZX_ERR_INVALID_ARGS;
    }
  }

  fbl::RefPtr<fs::Vnode> newparent;
  {
    std::lock_guard lock(vfs_lock_);
    if (ReadonlyLocked()) {
      return ZX_ERR_ACCESS_DENIED;
    }
    if (zx_status_t status = TokenToVnode(std::move(token), &newparent); status != ZX_OK) {
      return status;
    }

    if (zx_status_t status =
            oldparent->Rename(newparent, oldStr, newStr, old_must_be_dir, new_must_be_dir);
        status != ZX_OK) {
      return status;
    }
  }
  oldparent->Notify(oldStr, fio::wire::WatchEvent::kRemoved);
  newparent->Notify(newStr, fio::wire::WatchEvent::kAdded);
  return ZX_OK;
}

zx::status<FilesystemInfo> FuchsiaVfs::GetFilesystemInfo() {
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx_status_t FuchsiaVfs::Link(zx::event token, fbl::RefPtr<Vnode> oldparent, std::string_view oldStr,
                             std::string_view newStr) {
  std::lock_guard lock(vfs_lock_);
  fbl::RefPtr<fs::Vnode> newparent;
  if (zx_status_t status = TokenToVnode(std::move(token), &newparent); status != ZX_OK) {
    return status;
  }
  // Local filesystem
  if (ReadonlyLocked()) {
    return ZX_ERR_ACCESS_DENIED;
  }
  {
    zx::status result = TrimName(oldStr);
    if (result.is_error()) {
      return result.status_value();
    }
    if (result.value()) {
      return ZX_ERR_NOT_DIR;
    }
    if (oldStr == ".") {
      return ZX_ERR_UNAVAILABLE;
    }
    if (oldStr == "..") {
      return ZX_ERR_INVALID_ARGS;
    }
  }
  {
    zx::status result = TrimName(newStr);
    if (result.is_error()) {
      return result.status_value();
    }
    if (result.value()) {
      return ZX_ERR_NOT_DIR;
    }
    if (newStr == "." || newStr == "..") {
      return ZX_ERR_INVALID_ARGS;
    }
  }

  // Look up the target vnode
  fbl::RefPtr<Vnode> target;
  if (zx_status_t status = oldparent->Lookup(oldStr, &target); status != ZX_OK) {
    return status;
  }
  if (zx_status_t status = newparent->Link(newStr, target); status != ZX_OK) {
    return status;
  }
  newparent->Notify(newStr, fio::wire::WatchEvent::kAdded);
  return ZX_OK;
}

zx_status_t FuchsiaVfs::Serve(fbl::RefPtr<Vnode> vnode, zx::channel server_end,
                              VnodeConnectionOptions options) {
  zx::status result = vnode->ValidateOptions(options);
  if (result.is_error()) {
    return result.status_value();
  }
  return Serve(std::move(vnode), std::move(server_end), result.value());
}

zx_status_t FuchsiaVfs::AddInotifyFilterToVnode(fbl::RefPtr<Vnode> vnode,
                                                const fbl::RefPtr<Vnode>& parent_vnode,
                                                fuchsia_io::wire::InotifyWatchMask filter,
                                                uint32_t watch_descriptor, zx::socket socket) {
  // TODO we need parent vnode for inotify events when a directory is being watched for events on
  // its directory entries.
  vnode->InsertInotifyFilter(filter, watch_descriptor, std::move(socket));
  return ZX_OK;
}

zx_status_t FuchsiaVfs::Serve(fbl::RefPtr<Vnode> vnode, zx::channel server_end,
                              Vnode::ValidatedOptions options) {
  // At this point, the protocol that will be spoken over |server_end| is not
  // yet determined.
  //
  // To determine the protocol, we pick one that is both requested by the user
  // and supported by the vnode, deferring to |Vnode::Negotiate| if there are
  // multiple.
  //
  // In addition, if the |describe| option is set, then the channel always first
  // speaks the |fuchsia.io/Node| protocol, and then switches to the determined
  // protocol after sending the initial event.

  auto candidate_protocols = options->protocols() & vnode->GetProtocols();
  // |ValidateOptions| was called, hence at least one protocol must be supported.
  ZX_DEBUG_ASSERT(candidate_protocols.any());
  auto maybe_protocol = candidate_protocols.which();
  VnodeProtocol protocol;
  if (maybe_protocol.has_value()) {
    protocol = maybe_protocol.value();
  } else {
    protocol = vnode->Negotiate(candidate_protocols);
  }

  // Send an |fuchsia.io/OnOpen| event if requested.
  if (options->flags.describe) {
    zx::status<VnodeRepresentation> result = internal::Describe(vnode, protocol, *options);
    if (result.is_error()) {
      // Ignore errors since there is nothing we can do if this fails.
      [[maybe_unused]] auto unused_result =
          fidl::WireSendEvent(fidl::ServerEnd<fuchsia_io::Node>(std::move(server_end)))
              ->OnOpen(result.status_value(), fio::wire::NodeInfoDeprecated());
      return result.status_value();
    }
    ConvertToIoV1NodeInfo(std::move(result).value(), [&](fio::wire::NodeInfoDeprecated&& info) {
      // The channel may switch from |Node| protocol back to a custom protocol, after sending the
      // event, in the case of |VnodeProtocol::kConnector|.
      fidl::ServerEnd<fuchsia_io::Node> typed_server_end(std::move(server_end));
      // We ignore the error and continue here in case the far end has queued open requests and
      // immediately closed the connection.  If the caller is doing that, they shouldn't have used
      // the describe flag, but there have been cases where this happened in the past and so we
      // preserve that behaviour for now.
      [[maybe_unused]] auto result =
          fidl::WireSendEvent(typed_server_end)->OnOpen(ZX_OK, std::move(info));
      server_end = typed_server_end.TakeChannel();
    });
  }

  // If |node_reference| is specified, serve |fuchsia.io/Node| even for |VnodeProtocol::kConnector|
  // nodes. Otherwise, connect the raw channel to the custom service.
  if (!options->flags.node_reference && protocol == VnodeProtocol::kConnector) {
    return vnode->ConnectService(std::move(server_end));
  }

  std::unique_ptr<internal::Connection> connection;
  zx_status_t status = ([&] {
    switch (protocol) {
      case VnodeProtocol::kFile:
      case VnodeProtocol::kTty: {
        zx::stream stream;
        zx_status_t status = vnode->CreateStream(ToStreamOptions(*options), &stream);
        if (status == ZX_OK) {
          connection = std::make_unique<internal::StreamFileConnection>(
              this, std::move(vnode), std::move(stream), protocol, *options);
          return ZX_OK;
        }
        if (status == ZX_ERR_NOT_SUPPORTED) {
          connection = std::make_unique<internal::RemoteFileConnection>(this, std::move(vnode),
                                                                        protocol, *options);
          return ZX_OK;
        }
        return status;
      }
      case VnodeProtocol::kDirectory:
        connection = std::make_unique<internal::DirectoryConnection>(this, std::move(vnode),
                                                                     protocol, *options);
        return ZX_OK;
      case VnodeProtocol::kConnector:
        connection =
            std::make_unique<internal::NodeConnection>(this, std::move(vnode), protocol, *options);
        return ZX_OK;
    }
#ifdef __GNUC__
    // GCC does not infer that the above switch statement will always return by handling all defined
    // enum members.
    __builtin_abort();
#endif
  }());
  if (status != ZX_OK) {
    return status;
  }

  return RegisterConnection(std::move(connection), std::move(server_end));
}

void FuchsiaVfs::OnConnectionClosedRemotely(internal::Connection* connection) {
  ZX_DEBUG_ASSERT(connection);

  UnregisterConnection(connection);
}

zx_status_t FuchsiaVfs::ServeDirectory(fbl::RefPtr<fs::Vnode> vn,
                                       fidl::ServerEnd<fuchsia_io::Directory> server_end,
                                       Rights rights) {
  VnodeConnectionOptions options;
  options.flags.directory = true;
  options.rights = rights;
  zx::status validated_options = vn->ValidateOptions(options);
  if (validated_options.is_error()) {
    return validated_options.status_value();
  }
  if (zx_status_t status = OpenVnode(validated_options.value(), &vn); status != ZX_OK) {
    return status;
  }

  return Serve(std::move(vn), server_end.TakeChannel(), validated_options.value());
}

zx_status_t FuchsiaVfs::ForwardOpenRemote(fbl::RefPtr<Vnode> vn, fidl::ServerEnd<fio::Node> channel,
                                          std::string_view path, VnodeConnectionOptions options,
                                          uint32_t mode) {
  auto h = vn->GetRemote();
  if (!h.is_valid()) {
    return ZX_ERR_NOT_FOUND;
  }

  return fidl::WireCall(h)
      ->Open(options.ToIoV1Flags(), mode, fidl::StringView::FromExternal(path), std::move(channel))
      .status();
}

}  // namespace fs
