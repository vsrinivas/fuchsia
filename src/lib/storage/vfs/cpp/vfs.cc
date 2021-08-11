// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/vfs.h"

#include <lib/fdio/watcher.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "src/lib/storage/vfs/cpp/debug.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

#ifdef __Fuchsia__

#include <fuchsia/io/llcpp/fidl.h>
#include <fuchsia/io2/llcpp/fidl.h>
#include <lib/zx/event.h>
#include <lib/zx/process.h>
#include <threads.h>
#include <zircon/assert.h>

#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>

#include "src/lib/storage/vfs/cpp/connection.h"
#include "src/lib/storage/vfs/cpp/directory_connection.h"
#include "src/lib/storage/vfs/cpp/mount_channel.h"
#include "src/lib/storage/vfs/cpp/node_connection.h"
#include "src/lib/storage/vfs/cpp/remote_file_connection.h"
#include "src/lib/storage/vfs/cpp/stream_file_connection.h"

namespace fio = fuchsia_io;
namespace fio2 = fuchsia_io2;

#endif

namespace fs {
namespace {

zx_status_t LookupNode(fbl::RefPtr<Vnode> vn, std::string_view name, fbl::RefPtr<Vnode>* out) {
  if (name == "..") {
    return ZX_ERR_INVALID_ARGS;
  } else if (name == ".") {
    *out = std::move(vn);
    return ZX_OK;
  }
  return vn->Lookup(name, out);
}

// Validate open flags as much as they can be validated independently of the target node.
zx_status_t PrevalidateOptions(VnodeConnectionOptions options) {
  if (!options.rights.write) {
    if (options.flags.truncate) {
      return ZX_ERR_INVALID_ARGS;
    }
  } else if (!options.rights.any()) {
    if (!options.flags.node_reference) {
      return ZX_ERR_INVALID_ARGS;
    }
  }
  return ZX_OK;
}

}  // namespace

Vfs::Vfs() = default;

Vfs::~Vfs() {
  // Keep owning references to each vnode in case the callbacks cause any nodes to be deleted.
  std::vector<fbl::RefPtr<Vnode>> nodes_to_notify;
  {
    // This lock should not be necessary since the destructor should be single-threaded but is good
    // for completeness.
    std::lock_guard lock(vfs_lock_);
    nodes_to_notify.reserve(live_nodes_.size());
    for (auto& node_ptr : live_nodes_)
      nodes_to_notify.push_back(fbl::RefPtr<Vnode>(node_ptr));
  }

  // Notify all nodes that we're getting deleted.
  for (auto& node : nodes_to_notify)
    node->WillDestroyVfs();
}

#ifdef __Fuchsia__

Vfs::Vfs(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

void Vfs::SetDispatcher(async_dispatcher_t* dispatcher) {
  ZX_ASSERT_MSG(!dispatcher_,
                "Vfs::SetDispatcher maybe only be called when dispatcher_ is not set.");
  dispatcher_ = dispatcher;
}

#endif

Vfs::OpenResult Vfs::Open(fbl::RefPtr<Vnode> vndir, std::string_view path,
                          VnodeConnectionOptions options, Rights parent_rights, uint32_t mode) {
  std::lock_guard lock(vfs_lock_);
  return OpenLocked(std::move(vndir), path, options, parent_rights, mode);
}

Vfs::OpenResult Vfs::OpenLocked(fbl::RefPtr<Vnode> vndir, std::string_view path,
                                VnodeConnectionOptions options, Rights parent_rights,
                                uint32_t mode) {
  FS_PRETTY_TRACE_DEBUG("VfsOpen: path='", Path(path.data(), path.size()), "' options=", options);
  zx_status_t r;
  if ((r = PrevalidateOptions(options)) != ZX_OK) {
    return r;
  }
  if ((r = Vfs::Walk(vndir, path, &vndir, &path)) < 0) {
    return r;
  }

  if (vndir->IsRemote()) {
    // remote filesystem, return handle and path to caller
    return OpenResult::Remote{.vnode = std::move(vndir), .path = path};
  }

  {
    bool must_be_dir = false;
    if ((r = TrimName(path, &path, &must_be_dir)) != ZX_OK) {
      return r;
    } else if (path == "..") {
      return ZX_ERR_INVALID_ARGS;
    }
    if (must_be_dir) {
      options.flags.directory = true;
    }
  }

  fbl::RefPtr<Vnode> vn;
  bool just_created = false;
  if (options.flags.create) {
    if ((r = EnsureExists(std::move(vndir), path, &vn, options, mode, parent_rights,
                          &just_created)) != ZX_OK) {
      return r;
    }
  } else {
    if ((r = LookupNode(std::move(vndir), path, &vn)) != ZX_OK) {
      return r;
    }
  }

  if (!options.flags.no_remote && vn->IsRemote()) {
    // Opening a mount point: Traverse across remote.
    return OpenResult::RemoteRoot{.vnode = std::move(vn)};
  }

  if (ReadonlyLocked() && options.rights.write) {
    return ZX_ERR_ACCESS_DENIED;
  }

  if (vn->Supports(fs::VnodeProtocol::kDirectory) &&
      (options.flags.posix_write || options.flags.posix_execute)) {
    // This is such that POSIX open() can open a directory with O_RDONLY, and still get the
    // write/execute right if the parent directory connection has the write/execute right
    // respectively.  With the execute right in particular, the resulting connection may be passed
    // to fdio_get_vmo_exec() which requires the execute right. This transfers write and execute
    // from the parent, if present.
    Rights inheritable_rights{};
    inheritable_rights.write = options.flags.posix_write;
    inheritable_rights.execute = options.flags.posix_execute;
    options.rights |= parent_rights & inheritable_rights;
  }
  auto validated_options = vn->ValidateOptions(options);
  if (validated_options.is_error()) {
    return validated_options.error();
  }

  // |node_reference| requests that we don't actually open the underlying Vnode, but use the
  // connection as a reference to the Vnode.
  if (!options.flags.node_reference && !just_created) {
    if ((r = OpenVnode(validated_options.value(), &vn)) != ZX_OK) {
      return r;
    }

    if (!options.flags.no_remote && vn->IsRemote()) {
      // |OpenVnode| redirected us to a remote vnode; traverse across mount point.
      return OpenResult::RemoteRoot{.vnode = std::move(vn)};
    }

    if (options.flags.truncate && ((r = vn->Truncate(0)) < 0)) {
      vn->Close();
      return r;
    }
  }

  return OpenResult::Ok{.vnode = std::move(vn), .validated_options = validated_options.value()};
}

zx_status_t Vfs::EnsureExists(fbl::RefPtr<Vnode> vndir, std::string_view path,
                              fbl::RefPtr<Vnode>* out_vn, fs::VnodeConnectionOptions options,
                              uint32_t mode, Rights parent_rights, bool* did_create) {
  zx_status_t status;
  if (options.flags.directory && !S_ISDIR(mode)) {
    return ZX_ERR_INVALID_ARGS;
  } else if (options.flags.not_directory && S_ISDIR(mode)) {
    return ZX_ERR_INVALID_ARGS;
  } else if (path == ".") {
    return ZX_ERR_INVALID_ARGS;
  } else if (ReadonlyLocked()) {
    return ZX_ERR_ACCESS_DENIED;
  } else if (!parent_rights.write) {
    return ZX_ERR_ACCESS_DENIED;
  }
  if ((status = vndir->Create(path, mode, out_vn)) != ZX_OK) {
    *did_create = false;
    if ((status == ZX_ERR_ALREADY_EXISTS) && !options.flags.fail_if_exists) {
      return LookupNode(std::move(vndir), path, out_vn);
    }
    if (status == ZX_ERR_NOT_SUPPORTED) {
      // Filesystem may not support create (like devfs) in which case we should still try to open()
      // the file,
      return LookupNode(std::move(vndir), path, out_vn);
    }
    return status;
  }
#ifdef __Fuchsia__
  vndir->Notify(path, fio::wire::kWatchEventAdded);
#endif
  *did_create = true;
  return ZX_OK;
}

Vfs::TraversePathResult Vfs::TraversePathFetchVnode(fbl::RefPtr<Vnode> vndir,
                                                    std::string_view path) {
  std::lock_guard lock(vfs_lock_);
  return TraversePathFetchVnodeLocked(std::move(vndir), path);
}

Vfs::TraversePathResult Vfs::TraversePathFetchVnodeLocked(fbl::RefPtr<Vnode> vndir,
                                                          std::string_view path) {
  FS_PRETTY_TRACE_DEBUG("VfsTraversePathFetchVnode: path='", Path(path.data(), path.size()));
  if (zx_status_t result = Vfs::Walk(vndir, path, &vndir, &path); result != ZX_OK) {
    return result;
  }

  if (vndir->IsRemote()) {
    // remote filesystem, return handle and path to caller
    return TraversePathResult::Remote{.vnode = std::move(vndir), .path = path};
  }

  zx_status_t result;
  {
    bool must_be_dir = false;
    if ((result = TrimName(path, &path, &must_be_dir)) != ZX_OK) {
      return result;
    } else if (path == "..") {
      return ZX_ERR_INVALID_ARGS;
    }
  }

  fbl::RefPtr<Vnode> vn;
  if ((result = LookupNode(std::move(vndir), path, &vn)) != ZX_OK) {
    return result;
  }

  if (vn->IsRemote()) {
    // Found a mount point: Traverse across remote.
    return TraversePathResult::RemoteRoot{.vnode = std::move(vn)};
  }

  return TraversePathResult::Ok{.vnode = std::move(vn)};
}

zx_status_t Vfs::UnlinkValidated(fbl::RefPtr<Vnode> vndir, std::string_view name,
                                 bool must_be_dir) {
  {
    std::lock_guard lock(vfs_lock_);
    if (ReadonlyLocked()) {
      return ZX_ERR_ACCESS_DENIED;
    } else {
      if (zx_status_t status = vndir->Unlink(name, must_be_dir); status != ZX_OK) {
        return status;
      }
    }
  }
#ifdef __Fuchsia__
  vndir->Notify(name, fio::wire::kWatchEventRemoved);
#endif
  return ZX_OK;
}

zx_status_t Vfs::Unlink(fbl::RefPtr<Vnode> vndir, std::string_view path) {
  bool must_be_dir;
  if (zx_status_t status = TrimName(path, &path, &must_be_dir); status != ZX_OK)
    return status;

  if (!vfs_valid_name(path))
    return ZX_ERR_INVALID_ARGS;

  return UnlinkValidated(vndir, path, must_be_dir);
}

#ifdef __Fuchsia__

#define TOKEN_RIGHTS (ZX_RIGHTS_BASIC)

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
  return stream_options;
}

}  // namespace

void Vfs::TokenDiscard(zx::event ios_token) {
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

zx_status_t Vfs::VnodeToToken(fbl::RefPtr<Vnode> vn, zx::event* ios_token, zx::event* out) {
  zx_status_t r;

  std::lock_guard lock(vfs_lock_);
  if (ios_token->is_valid()) {
    // Token has already been set for this iostate
    if ((r = ios_token->duplicate(TOKEN_RIGHTS, out) != ZX_OK)) {
      return r;
    }
    return ZX_OK;
  }

  zx::event new_token;
  zx::event new_ios_token;
  if ((r = zx::event::create(0, &new_ios_token)) != ZX_OK) {
    return r;
  } else if ((r = new_ios_token.duplicate(TOKEN_RIGHTS, &new_token) != ZX_OK)) {
    return r;
  }
  auto koid = GetTokenKoid(new_ios_token);
  vnode_tokens_.insert(std::make_unique<VnodeToken>(koid, std::move(vn)));
  *ios_token = std::move(new_ios_token);
  *out = std::move(new_token);
  return ZX_OK;
}

bool Vfs::IsTokenAssociatedWithVnode(zx::event token) {
  std::lock_guard lock(vfs_lock_);
  return TokenToVnode(std::move(token), nullptr) == ZX_OK;
}

zx_status_t Vfs::TokenToVnode(zx::event token, fbl::RefPtr<Vnode>* out) {
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

zx_status_t Vfs::Rename(zx::event token, fbl::RefPtr<Vnode> oldparent, std::string_view oldStr,
                        std::string_view newStr) {
  // Local filesystem
  bool old_must_be_dir;
  bool new_must_be_dir;
  zx_status_t r;
  if ((r = TrimName(oldStr, &oldStr, &old_must_be_dir)) != ZX_OK) {
    return r;
  } else if (oldStr == ".") {
    return ZX_ERR_UNAVAILABLE;
  } else if (oldStr == "..") {
    return ZX_ERR_INVALID_ARGS;
  }

  if ((r = TrimName(newStr, &newStr, &new_must_be_dir)) != ZX_OK) {
    return r;
  } else if (newStr == "." || newStr == "..") {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::RefPtr<fs::Vnode> newparent;
  {
    std::lock_guard lock(vfs_lock_);
    if (ReadonlyLocked()) {
      return ZX_ERR_ACCESS_DENIED;
    }
    if ((r = TokenToVnode(std::move(token), &newparent)) != ZX_OK) {
      return r;
    }

    r = oldparent->Rename(newparent, oldStr, newStr, old_must_be_dir, new_must_be_dir);
  }
  if (r != ZX_OK) {
    return r;
  }
  oldparent->Notify(oldStr, fio::wire::kWatchEventRemoved);
  newparent->Notify(newStr, fio::wire::kWatchEventAdded);
  return ZX_OK;
}

zx_status_t Vfs::Readdir(Vnode* vn, VdirCookie* cookie, void* dirents, size_t len,
                         size_t* out_actual) {
  std::lock_guard lock(vfs_lock_);
  return vn->Readdir(cookie, dirents, len, out_actual);
}

zx_status_t Vfs::Link(zx::event token, fbl::RefPtr<Vnode> oldparent, std::string_view oldStr,
                      std::string_view newStr) {
  std::lock_guard lock(vfs_lock_);
  fbl::RefPtr<fs::Vnode> newparent;
  zx_status_t r;
  if ((r = TokenToVnode(std::move(token), &newparent)) != ZX_OK) {
    return r;
  }
  // Local filesystem
  bool old_must_be_dir;
  bool new_must_be_dir;
  if (ReadonlyLocked()) {
    return ZX_ERR_ACCESS_DENIED;
  } else if ((r = TrimName(oldStr, &oldStr, &old_must_be_dir)) != ZX_OK) {
    return r;
  } else if (old_must_be_dir) {
    return ZX_ERR_NOT_DIR;
  } else if (oldStr == ".") {
    return ZX_ERR_UNAVAILABLE;
  } else if (oldStr == "..") {
    return ZX_ERR_INVALID_ARGS;
  }

  if ((r = TrimName(newStr, &newStr, &new_must_be_dir)) != ZX_OK) {
    return r;
  } else if (new_must_be_dir) {
    return ZX_ERR_NOT_DIR;
  } else if (newStr == "." || newStr == "..") {
    return ZX_ERR_INVALID_ARGS;
  }

  // Look up the target vnode
  fbl::RefPtr<Vnode> target;
  if ((r = oldparent->Lookup(oldStr, &target)) < 0) {
    return r;
  }
  r = newparent->Link(newStr, target);
  if (r != ZX_OK) {
    return r;
  }
  newparent->Notify(newStr, fio::wire::kWatchEventAdded);
  return ZX_OK;
}

zx_status_t Vfs::Serve(fbl::RefPtr<Vnode> vnode, fidl::ServerEnd<fuchsia_io::Node> channel,
                       VnodeConnectionOptions options) {
  auto result = vnode->ValidateOptions(options);
  if (result.is_error()) {
    return result.error();
  }
  return Serve(std::move(vnode), std::move(channel), result.value());
}

zx_status_t Vfs::AddInotifyFilterToVnode(fbl::RefPtr<Vnode> vnode,
                                         const fbl::RefPtr<Vnode>& parent_vnode,
                                         fio2::wire::InotifyWatchMask filter,
                                         uint32_t watch_descriptor, zx::socket socket) {
  // TODO we need parent vnode for inotify events when a directory is being watched for events on
  // its directory entries.
  vnode->InsertInotifyFilter(filter, watch_descriptor, std::move(socket));
  return ZX_OK;
}

zx_status_t Vfs::Serve(fbl::RefPtr<Vnode> vnode, fidl::ServerEnd<fuchsia_io::Node> server_end,
                       Vnode::ValidatedOptions options) {
  // |ValidateOptions| was called, hence at least one protocol must be supported.
  auto candidate_protocols = options->protocols() & vnode->GetProtocols();
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
    fpromise::result<VnodeRepresentation, zx_status_t> result =
        internal::Describe(vnode, protocol, *options);
    if (result.is_error()) {
      fidl::WireEventSender<fio::Node>(std::move(server_end))
          .OnOpen(result.error(), fio::wire::NodeInfo());
      return result.error();
    }
    ConvertToIoV1NodeInfo(result.take_value(), [&](fio::wire::NodeInfo&& info) {
      // The channel may switch from |Node| protocol back to a custom protocol, after sending the
      // event, in the case of |VnodeProtocol::kConnector|.
      fidl::WireEventSender<fio::Node> event_sender{std::move(server_end)};
      event_sender.OnOpen(ZX_OK, std::move(info));
      server_end = std::move(event_sender.server_end());
    });
  }

  // If |node_reference| is specified, serve |fuchsia.io/Node| even for |VnodeProtocol::kConnector|
  // nodes. Otherwise, connect the raw channel to the custom service.
  if (!options->flags.node_reference && protocol == VnodeProtocol::kConnector) {
    return vnode->ConnectService(server_end.TakeChannel());
  }

  std::unique_ptr<internal::Connection> connection;
  zx_status_t status = ([&] {
    switch (protocol) {
      case VnodeProtocol::kFile:
      case VnodeProtocol::kDevice:
      case VnodeProtocol::kTty:
      // In memfs and bootfs, memory objects (vmo-files) appear to support |fuchsia.io/File.Read|.
      // Therefore choosing a file connection here is the closest approximation.
      case VnodeProtocol::kMemory: {
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
      case VnodeProtocol::kPipe:
        connection =
            std::make_unique<internal::NodeConnection>(this, std::move(vnode), protocol, *options);
        return ZX_OK;
      case VnodeProtocol::kDatagramSocket:
        // The posix socket protocol is served by netstack.
        ZX_PANIC("fuchsia.posix.socket/DatagramSocket is not implemented");
      case VnodeProtocol::kStreamSocket:
        // The posix socket protocol is served by netstack.
        ZX_PANIC("fuchsia.posix.socket/StreamSocket is not implemented");
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

  return RegisterConnection(std::move(connection), server_end.TakeChannel());
}

void Vfs::OnConnectionClosedRemotely(internal::Connection* connection) {
  ZX_DEBUG_ASSERT(connection);

  UnregisterConnection(connection);
}

zx_status_t Vfs::ServeDirectory(fbl::RefPtr<fs::Vnode> vn,
                                fidl::ServerEnd<fuchsia_io::Directory> server_end, Rights rights) {
  VnodeConnectionOptions options;
  options.flags.directory = true;
  options.rights = rights;
  auto validated_options = vn->ValidateOptions(options);
  if (validated_options.is_error()) {
    return validated_options.error();
  } else if (zx_status_t r = OpenVnode(validated_options.value(), &vn); r != ZX_OK) {
    return r;
  }

  return Serve(std::move(vn), server_end.TakeChannel(), validated_options.value());
}

#endif  // ifdef __Fuchsia__

void Vfs::RegisterVnode(Vnode* vnode) {
  std::lock_guard lock(live_nodes_lock_);

  // Should not be registered twice.
  ZX_DEBUG_ASSERT(live_nodes_.find(vnode) == live_nodes_.end());
  live_nodes_.insert(vnode);
}

void Vfs::UnregisterVnode(Vnode* vnode) {
  std::lock_guard lock(live_nodes_lock_);
  UnregisterVnodeLocked(vnode);
}

void Vfs::UnregisterVnodeLocked(Vnode* vnode) {
  auto found = live_nodes_.find(vnode);
  ZX_DEBUG_ASSERT(found != live_nodes_.end());  // Should always be registered first.
  live_nodes_.erase(found);
}

zx_status_t Vfs::TrimName(std::string_view name, std::string_view* name_out, bool* is_dir_out) {
  *is_dir_out = false;

  size_t len = name.length();
  while ((len > 0) && name[len - 1] == '/') {
    len--;
    *is_dir_out = true;
  }

  if (len == 0) {
    // 'name' should not contain paths consisting of exclusively '/' characters.
    return ZX_ERR_INVALID_ARGS;
  } else if (len > NAME_MAX) {
    // Name must be less than the maximum-expected length.
    return ZX_ERR_BAD_PATH;
  } else if (memchr(name.data(), '/', len) != nullptr) {
    // Name must not contain '/' characters after being trimmed.
    return ZX_ERR_INVALID_ARGS;
  }

  *name_out = std::string_view(name.data(), len);
  return ZX_OK;
}

void Vfs::SetReadonly(bool value) {
  std::lock_guard lock(vfs_lock_);
  readonly_ = value;
}

zx_status_t Vfs::Walk(fbl::RefPtr<Vnode> vn, std::string_view path, fbl::RefPtr<Vnode>* out_vn,
                      std::string_view* out_path) {
  zx_status_t r;

  if (path.empty()) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Handle "." and "/".
  if (path == "." || path == "/") {
    *out_vn = std::move(vn);
    *out_path = ".";
    return ZX_OK;
  }

  // Allow leading '/'.
  if (path[0] == '/') {
    path = path.substr(1);
  }

  // Allow trailing '/', but only if preceded by something.
  if (path.length() > 1 && path.back() == '/') {
    path = path.substr(0, path.length() - 1);
  }

  for (;;) {
    if (vn->IsRemote()) {
      // Remote filesystem mount, caller must resolve.
      *out_vn = std::move(vn);
      *out_path = path;
      return ZX_OK;
    }

    // Look for the next '/' separated path component.
    size_t slash = path.find('/');
    std::string_view component = path.substr(0, slash);
    if (component.length() > NAME_MAX) {
      return ZX_ERR_BAD_PATH;
    }
    if (component.empty() || component == "." || component == "..") {
      return ZX_ERR_INVALID_ARGS;
    }

    if (slash == std::string_view::npos) {
      // Final path segment.
      *out_vn = vn;
      *out_path = path;
      return ZX_OK;
    }

    if ((r = LookupNode(std::move(vn), component, &vn)) != ZX_OK) {
      return r;
    }

    // Traverse to the next segment.
    path = path.substr(slash + 1);
  }
}

}  // namespace fs
