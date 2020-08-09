// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/watcher.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <memory>
#include <utility>

#include <fbl/auto_call.h>
#include <fs/debug.h>
#include <fs/trace.h>
#include <fs/vfs.h>
#include <fs/vnode.h>

#ifdef __Fuchsia__

#include <lib/zx/event.h>
#include <lib/zx/process.h>
#include <threads.h>
#include <zircon/assert.h>

#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>
#include <fs/internal/connection.h>
#include <fs/internal/directory_connection.h>
#include <fs/internal/node_connection.h>
#include <fs/internal/remote_file_connection.h>
#include <fs/internal/stream_file_connection.h>
#include <fs/mount_channel.h>

namespace fio = ::llcpp::fuchsia::io;

#endif

namespace fs {
namespace {

// Trim a name before sending it to internal filesystem functions.
// Trailing '/' characters imply that the name must refer to a directory.
zx_status_t TrimName(fbl::StringPiece name, fbl::StringPiece* name_out, bool* dir_out) {
  size_t len = name.length();
  bool is_dir = false;
  while ((len > 0) && name[len - 1] == '/') {
    len--;
    is_dir = true;
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

  *name_out = fbl::StringPiece(name.data(), len);
  *dir_out = is_dir;
  return ZX_OK;
}

zx_status_t LookupNode(fbl::RefPtr<Vnode> vn, fbl::StringPiece name, fbl::RefPtr<Vnode>* out) {
  if (name == "..") {
    return ZX_ERR_INVALID_ARGS;
  } else if (name == ".") {
    *out = std::move(vn);
    return ZX_OK;
  }
  return vn->Lookup(out, name);
}

// Validate open flags as much as they can be validated
// independently of the target node.
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
Vfs::~Vfs() = default;

#ifdef __Fuchsia__

Vfs::Vfs(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

void Vfs::SetDispatcher(async_dispatcher_t* dispatcher) {
  ZX_ASSERT_MSG(!dispatcher_,
                "Vfs::SetDispatcher maybe only be called when dispatcher_ is not set.");
  dispatcher_ = dispatcher;
}

#endif

Vfs::OpenResult Vfs::Open(fbl::RefPtr<Vnode> vndir, fbl::StringPiece path,
                          VnodeConnectionOptions options, Rights parent_rights, uint32_t mode) {
#ifdef __Fuchsia__
  fbl::AutoLock lock(&vfs_lock_);
#endif
  return OpenLocked(std::move(vndir), path, options, parent_rights, mode);
}

Vfs::OpenResult Vfs::OpenLocked(fbl::RefPtr<Vnode> vndir, fbl::StringPiece path,
                                VnodeConnectionOptions options, Rights parent_rights,
                                uint32_t mode) {
  FS_PRETTY_TRACE_DEBUG("VfsOpen: path='", Path(path.data(), path.size()), "' options=", options);
  zx_status_t r;
  if ((r = PrevalidateOptions(options)) != ZX_OK) {
    return r;
  }
  if ((r = Vfs::Walk(vndir, &vndir, path, &path)) < 0) {
    return r;
  }
#ifdef __Fuchsia__
  if (vndir->IsRemote()) {
    // remote filesystem, return handle and path to caller
    return OpenResult::Remote{.vnode = std::move(vndir), .path = path};
  }
#endif

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
    if ((r = EnsureExists(std::move(vndir), path, &vn, options, mode, &just_created)) != ZX_OK) {
      return r;
    }
  } else {
    if ((r = LookupNode(std::move(vndir), path, &vn)) != ZX_OK) {
      return r;
    }
  }

#ifdef __Fuchsia__
  if (!options.flags.no_remote && vn->IsRemote()) {
    // Opening a mount point: Traverse across remote.
    return OpenResult::RemoteRoot{.vnode = std::move(vn)};
  }
#endif

  if (ReadonlyLocked() && options.rights.write) {
    return ZX_ERR_ACCESS_DENIED;
  }

  if (vn->Supports(fs::VnodeProtocol::kDirectory) && options.flags.posix) {
    // Save this before modifying |options| below.
    bool admin = options.rights.admin;

    // This is such that POSIX open() can open a directory with O_RDONLY, and
    // still get the write/execute right if the parent directory connection has the
    // write/execute right respectively.  With the execute right in particular, the resulting
    // connection may be passed to fdio_get_vmo_exec() which requires the execute right.
    // This transfers write and execute from the parent, if present.
    auto inheritable_rights = Rights::WriteExec();
    options.rights |= parent_rights & inheritable_rights;

    // The ADMIN right is not inherited. It must be explicitly specified.
    options.rights.admin = admin;
  }
  auto validated_options = vn->ValidateOptions(options);
  if (validated_options.is_error()) {
    return validated_options.error();
  }

  // |node_reference| requests that we don't actually open the underlying Vnode,
  // but use the connection as a reference to the Vnode.
  if (!options.flags.node_reference && !just_created) {
    if ((r = OpenVnode(validated_options.value(), &vn)) != ZX_OK) {
      return r;
    }
#ifdef __Fuchsia__
    if (!options.flags.no_remote && vn->IsRemote()) {
      // |OpenVnode| redirected us to a remote vnode; traverse across mount point.
      return OpenResult::RemoteRoot{.vnode = std::move(vn)};
    }
#endif
    if (options.flags.truncate && ((r = vn->Truncate(0)) < 0)) {
      vn->Close();
      return r;
    }
  }

  FS_TRACE_DEBUG("VfsOpen: vn=%p\n", vn.get());
  return OpenResult::Ok{.vnode = std::move(vn), .validated_options = validated_options.value()};
}

zx_status_t Vfs::EnsureExists(fbl::RefPtr<Vnode> vndir, fbl::StringPiece path,
                              fbl::RefPtr<Vnode>* out_vn, fs::VnodeConnectionOptions options,
                              uint32_t mode, bool* did_create) {
  zx_status_t status;
  if (options.flags.directory && !S_ISDIR(mode)) {
    return ZX_ERR_INVALID_ARGS;
  } else if (options.flags.not_directory && S_ISDIR(mode)) {
    return ZX_ERR_INVALID_ARGS;
  } else if (path == ".") {
    return ZX_ERR_INVALID_ARGS;
  } else if (ReadonlyLocked()) {
    return ZX_ERR_ACCESS_DENIED;
  }
  if ((status = vndir->Create(out_vn, path, mode)) != ZX_OK) {
    *did_create = false;
    if ((status == ZX_ERR_ALREADY_EXISTS) && !options.flags.fail_if_exists) {
      return LookupNode(std::move(vndir), path, out_vn);
    }
    if (status == ZX_ERR_NOT_SUPPORTED) {
      // filesystem may not support create (like devfs)
      // in which case we should still try to open() the file
      return LookupNode(std::move(vndir), path, out_vn);
    }
    return status;
  }
#ifdef __Fuchsia__
  vndir->Notify(path, fio::WATCH_EVENT_ADDED);
#endif
  *did_create = true;
  return ZX_OK;
}

zx_status_t Vfs::Unlink(fbl::RefPtr<Vnode> vndir, fbl::StringPiece path) {
  bool must_be_dir;
  zx_status_t r;
  if ((r = TrimName(path, &path, &must_be_dir)) != ZX_OK) {
    return r;
  } else if (path == ".") {
    return ZX_ERR_UNAVAILABLE;
  } else if (path == "..") {
    return ZX_ERR_INVALID_ARGS;
  }

  {
#ifdef __Fuchsia__
    fbl::AutoLock lock(&vfs_lock_);
#endif
    if (ReadonlyLocked()) {
      r = ZX_ERR_ACCESS_DENIED;
    } else {
      r = vndir->Unlink(path, must_be_dir);
    }
  }
  if (r != ZX_OK) {
    return r;
  }
#ifdef __Fuchsia__
  vndir->Notify(path, fio::WATCH_EVENT_REMOVED);
#endif
  return ZX_OK;
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
  fbl::AutoLock lock(&vfs_lock_);
  if (ios_token) {
    // The token is cleared here to prevent the following race condition:
    // 1) Open
    // 2) GetToken
    // 3) Close + Release Vnode
    // 4) Use token handle to access defunct vnode (or a different vnode,
    //    if the memory for it is reallocated).
    //
    // By cleared the token cookie, any remaining handles to the event will
    // be ignored by the filesystem server.
    auto rename_request = vnode_tokens_.erase(GetTokenKoid(ios_token));
  }
}

zx_status_t Vfs::VnodeToToken(fbl::RefPtr<Vnode> vn, zx::event* ios_token, zx::event* out) {
  zx_status_t r;

  fbl::AutoLock lock(&vfs_lock_);
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
  fbl::AutoLock lock(&vfs_lock_);
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

zx_status_t Vfs::Rename(zx::event token, fbl::RefPtr<Vnode> oldparent, fbl::StringPiece oldStr,
                        fbl::StringPiece newStr) {
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
    fbl::AutoLock lock(&vfs_lock_);
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
  oldparent->Notify(oldStr, fio::WATCH_EVENT_REMOVED);
  newparent->Notify(newStr, fio::WATCH_EVENT_ADDED);
  return ZX_OK;
}

zx_status_t Vfs::Readdir(Vnode* vn, vdircookie_t* cookie, void* dirents, size_t len,
                         size_t* out_actual) {
  fbl::AutoLock lock(&vfs_lock_);
  return vn->Readdir(cookie, dirents, len, out_actual);
}

zx_status_t Vfs::Link(zx::event token, fbl::RefPtr<Vnode> oldparent, fbl::StringPiece oldStr,
                      fbl::StringPiece newStr) {
  fbl::AutoLock lock(&vfs_lock_);
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
  if ((r = oldparent->Lookup(&target, oldStr)) < 0) {
    return r;
  }
  r = newparent->Link(newStr, target);
  if (r != ZX_OK) {
    return r;
  }
  newparent->Notify(newStr, fio::WATCH_EVENT_ADDED);
  return ZX_OK;
}

zx_status_t Vfs::Serve(fbl::RefPtr<Vnode> vnode, zx::channel channel,
                       VnodeConnectionOptions options) {
  auto result = vnode->ValidateOptions(options);
  if (result.is_error()) {
    return result.error();
  }
  return Serve(std::move(vnode), std::move(channel), result.value());
}

zx_status_t Vfs::Serve(fbl::RefPtr<Vnode> vnode, zx::channel channel,
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
    fit::result<VnodeRepresentation, zx_status_t> result =
        internal::Describe(vnode, protocol, *options);
    if (result.is_error()) {
      fio::Node::SendOnOpenEvent(zx::unowned_channel(channel), result.error(), fio::NodeInfo());
      return result.error();
    }
    ConvertToIoV1NodeInfo(result.take_value(), [&](fio::NodeInfo&& info) {
      fio::Node::SendOnOpenEvent(zx::unowned_channel(channel), ZX_OK, std::move(info));
    });
  }

  // If |node_reference| is specified, serve |fuchsia.io/Node| even for
  // |VnodeProtocol::kConnector| nodes.
  if (!options->flags.node_reference && protocol == VnodeProtocol::kConnector) {
    return vnode->ConnectService(std::move(channel));
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
    // GCC does not infer that the above switch statement will always return by
    // handling all defined enum members.
    __builtin_abort();
#endif
  }());
  if (status != ZX_OK) {
    return status;
  }

  return RegisterConnection(std::move(connection), std::move(channel));
}

void Vfs::OnConnectionClosedRemotely(internal::Connection* connection) {
  ZX_DEBUG_ASSERT(connection);

  UnregisterConnection(connection);
}

zx_status_t Vfs::ServeDirectory(fbl::RefPtr<fs::Vnode> vn, zx::channel channel, Rights rights) {
  VnodeConnectionOptions options;
  options.flags.directory = true;
  options.rights = rights;
  auto validated_options = vn->ValidateOptions(options);
  if (validated_options.is_error()) {
    return validated_options.error();
  } else if (zx_status_t r = OpenVnode(validated_options.value(), &vn); r != ZX_OK) {
    return r;
  }

  // Tell the calling process that we've mounted the directory.
  zx_status_t r = channel.signal_peer(0, ZX_USER_SIGNAL_0);
  // ZX_ERR_PEER_CLOSED is ok because the channel may still be readable.
  if (r != ZX_OK && r != ZX_ERR_PEER_CLOSED) {
    return r;
  }

  return Serve(std::move(vn), std::move(channel), validated_options.value());
}

#endif  // ifdef __Fuchsia__

void Vfs::SetReadonly(bool value) {
#ifdef __Fuchsia__
  fbl::AutoLock lock(&vfs_lock_);
#endif
  readonly_ = value;
}

zx_status_t Vfs::Walk(fbl::RefPtr<Vnode> vn, fbl::RefPtr<Vnode>* out_vn, fbl::StringPiece path,
                      fbl::StringPiece* out_path) {
  zx_status_t r;
  while (!path.empty() && path[path.length() - 1] == '/') {
    // Discard extra trailing '/' characters.
    path = fbl::StringPiece(path.data(), path.length() - 1);
  }

  for (;;) {
    while (!path.empty() && path[0] == '/') {
      // Discard extra leading '/' characters.
      path = fbl::StringPiece(&path[1], path.length() - 1);
    }
    if (path.empty()) {
      // Convert empty initial path of final path segment to ".".
      path = fbl::StringPiece(".", 1);
    }
#ifdef __Fuchsia__
    if (vn->IsRemote()) {
      // Remote filesystem mount, caller must resolve.
      *out_vn = std::move(vn);
      *out_path = std::move(path);
      return ZX_OK;
    }
#endif

    // Look for the next '/' separated path component.
    const char* next_path = reinterpret_cast<const char*>(memchr(path.data(), '/', path.length()));
    if (next_path == nullptr) {
      // Final path segment.
      *out_vn = vn;
      *out_path = path;
      return ZX_OK;
    }

    // Path has at least one additional segment.
    fbl::StringPiece component(path.data(), next_path - path.data());
    if (component.length() > NAME_MAX) {
      return ZX_ERR_BAD_PATH;
    }
    if ((r = LookupNode(std::move(vn), component, &vn)) != ZX_OK) {
      return r;
    }
    // Traverse to the next segment.
    path = fbl::StringPiece(next_path + 1, path.length() - (component.length() + 1));
  }
}

}  // namespace fs
