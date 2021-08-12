// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/fuchsia_vfs.h"

#include <fuchsia/io/llcpp/fidl.h>
#include <fuchsia/io2/llcpp/fidl.h>
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
#include "src/lib/storage/vfs/cpp/mount_channel.h"
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
  return stream_options;
}

}  // namespace

constexpr FuchsiaVfs::MountNode::MountNode() = default;
FuchsiaVfs::MountNode::~MountNode() { ZX_DEBUG_ASSERT(vn_ == nullptr); }

void FuchsiaVfs::MountNode::SetNode(fbl::RefPtr<Vnode> vn) {
  ZX_DEBUG_ASSERT(vn_ == nullptr);
  vn_ = vn;
}

fidl::ClientEnd<fio::Directory> FuchsiaVfs::MountNode::ReleaseRemote() {
  ZX_DEBUG_ASSERT(vn_ != nullptr);
  fidl::ClientEnd<fio::Directory> h = vn_->DetachRemote();
  vn_ = nullptr;
  return h;
}

bool FuchsiaVfs::MountNode::VnodeMatch(fbl::RefPtr<Vnode> vn) const {
  ZX_DEBUG_ASSERT(vn_ != nullptr);
  return vn == vn_;
}

FuchsiaVfs::FuchsiaVfs(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

FuchsiaVfs::~FuchsiaVfs() = default;

void FuchsiaVfs::SetDispatcher(async_dispatcher_t* dispatcher) {
  ZX_ASSERT_MSG(!dispatcher_,
                "FuchsiaVfs::SetDispatcher maybe only be called when dispatcher_ is not set.");
  dispatcher_ = dispatcher;
}

zx_status_t FuchsiaVfs::UnlinkValidated(fbl::RefPtr<Vnode> vndir, std::string_view name,
                                        bool must_be_dir) {
  if (zx_status_t s = Vfs::UnlinkValidated(vndir, name, must_be_dir); s != ZX_OK)
    return s;

  vndir->Notify(name, fio::wire::kWatchEventRemoved);
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
  zx_status_t r;

  std::lock_guard lock(vfs_lock_);
  if (ios_token->is_valid()) {
    // Token has already been set for this iostate
    if ((r = ios_token->duplicate(ZX_RIGHTS_BASIC, out) != ZX_OK)) {
      return r;
    }
    return ZX_OK;
  }

  zx::event new_token;
  zx::event new_ios_token;
  if ((r = zx::event::create(0, &new_ios_token)) != ZX_OK) {
    return r;
  } else if ((r = new_ios_token.duplicate(ZX_RIGHTS_BASIC, &new_token) != ZX_OK)) {
    return r;
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

zx_status_t FuchsiaVfs::EnsureExists(fbl::RefPtr<Vnode> vndir, std::string_view path,
                                     fbl::RefPtr<Vnode>* out_vn, fs::VnodeConnectionOptions options,
                                     uint32_t mode, Rights parent_rights, bool* did_create) {
  if (zx_status_t s =
          Vfs::EnsureExists(vndir, path, out_vn, options, mode, parent_rights, did_create);
      s != ZX_OK)
    return s;

  vndir->Notify(path, fio::wire::kWatchEventAdded);
  return ZX_OK;
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

zx_status_t FuchsiaVfs::Link(zx::event token, fbl::RefPtr<Vnode> oldparent, std::string_view oldStr,
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

zx_status_t FuchsiaVfs::Serve(fbl::RefPtr<Vnode> vnode, fidl::ServerEnd<fuchsia_io::Node> channel,
                              VnodeConnectionOptions options) {
  auto result = vnode->ValidateOptions(options);
  if (result.is_error()) {
    return result.error();
  }
  return Serve(std::move(vnode), std::move(channel), result.value());
}

zx_status_t FuchsiaVfs::AddInotifyFilterToVnode(fbl::RefPtr<Vnode> vnode,
                                                const fbl::RefPtr<Vnode>& parent_vnode,
                                                fuchsia_io2::wire::InotifyWatchMask filter,
                                                uint32_t watch_descriptor, zx::socket socket) {
  // TODO we need parent vnode for inotify events when a directory is being watched for events on
  // its directory entries.
  vnode->InsertInotifyFilter(filter, watch_descriptor, std::move(socket));
  return ZX_OK;
}

zx_status_t FuchsiaVfs::Serve(fbl::RefPtr<Vnode> vnode,
                              fidl::ServerEnd<fuchsia_io::Node> server_end,
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
  auto validated_options = vn->ValidateOptions(options);
  if (validated_options.is_error()) {
    return validated_options.error();
  } else if (zx_status_t r = OpenVnode(validated_options.value(), &vn); r != ZX_OK) {
    return r;
  }

  return Serve(std::move(vn), server_end.TakeChannel(), validated_options.value());
}

// Installs a remote filesystem on vn and adds it to the remote_list_.
zx_status_t FuchsiaVfs::InstallRemote(fbl::RefPtr<Vnode> vn, MountChannel h) {
  if (vn == nullptr) {
    return ZX_ERR_ACCESS_DENIED;
  }

  // Allocate a node to track the remote handle
  fbl::AllocChecker ac;
  std::unique_ptr<MountNode> mount_point(new (&ac) MountNode());
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  zx_status_t status = vn->AttachRemote(std::move(h));
  if (status != ZX_OK) {
    return status;
  }
  // Save this node in the list of mounted vnodes
  mount_point->SetNode(std::move(vn));
  std::lock_guard lock(vfs_lock_);
  remote_list_.push_front(std::move(mount_point));
  return ZX_OK;
}

zx_status_t FuchsiaVfs::UninstallRemote(fbl::RefPtr<Vnode> vn, fidl::ClientEnd<fio::Directory>* h) {
  std::lock_guard lock(vfs_lock_);
  return UninstallRemoteLocked(std::move(vn), h);
}

zx_status_t FuchsiaVfs::MountMkdir(fbl::RefPtr<Vnode> vn, std::string_view name, MountChannel h,
                                   uint32_t flags) {
  std::lock_guard lock(vfs_lock_);
  return OpenLocked(
             vn, name,
             fs::VnodeConnectionOptions::ReadOnly().set_create().set_directory().set_no_remote(),
             fs::Rights::ReadWrite(), S_IFDIR)
      .visit([&](auto&& result) __TA_REQUIRES(vfs_lock_) {
        using T = std::decay_t<decltype(result)>;
        using OpenResult = fs::FuchsiaVfs::OpenResult;
        if constexpr (std::is_same_v<T, OpenResult::Error>) {
          return result;
        } else {
          if (result.vnode->IsRemote()) {
            if (flags & fio::wire::kMountCreateFlagReplace) {
              // There is an old remote handle on this vnode; shut it down and replace it with our
              // own.
              fidl::ClientEnd<fio::Directory> old_remote;
              FuchsiaVfs::UninstallRemoteLocked(vn, &old_remote);
              // Passing |zx::time::infinite_past()| results in a fire-and-forget call.
              // TODO(fxbug.dev/42264): Add proper tracking of remote filesystem teardown.
              // Note: this is best-effort, and would fail if the remote endpoint does not speak the
              // |fuchsia.io/DirectoryAdmin| protocol.
              fidl::ClientEnd<fio::DirectoryAdmin> old_remote_admin(old_remote.TakeChannel());
              FuchsiaVfs::UnmountHandle(std::move(old_remote_admin), zx::time::infinite_past());
            } else {
              return ZX_ERR_BAD_STATE;
            }
          }
          return FuchsiaVfs::InstallRemoteLocked(result.vnode, std::move(h));
        }
      });
}

zx_status_t FuchsiaVfs::ForwardOpenRemote(fbl::RefPtr<Vnode> vn, fidl::ServerEnd<fio::Node> channel,
                                          std::string_view path, VnodeConnectionOptions options,
                                          uint32_t mode) {
  std::lock_guard lock(vfs_lock_);
  auto h = vn->GetRemote();
  if (!h.is_valid()) {
    return ZX_ERR_NOT_FOUND;
  }

  auto r = fidl::WireCall(h)
               .Open(options.ToIoV1Flags(), mode, fidl::StringView::FromExternal(path),
                     std::move(channel))
               .status();
  if (r == ZX_ERR_PEER_CLOSED) {
    fidl::ClientEnd<fio::Directory> c;
    UninstallRemoteLocked(std::move(vn), &c);
  }
  return r;
}

// Uninstall all remote filesystems. Acts like 'UninstallRemote' for all known remotes.
zx_status_t FuchsiaVfs::UninstallAll(zx::time deadline) {
  std::unique_ptr<MountNode> mount_point;
  for (;;) {
    {
      std::lock_guard lock(vfs_lock_);
      mount_point = remote_list_.pop_front();
    }
    if (mount_point) {
      // Note: this is best-effort, and would fail if the remote endpoint does not speak the
      // |fuchsia.io/DirectoryAdmin| protocol.
      fidl::ClientEnd<fio::DirectoryAdmin> mount_admin(mount_point->ReleaseRemote().TakeChannel());
      FuchsiaVfs::UnmountHandle(std::move(mount_admin), deadline);
    } else {
      return ZX_OK;
    }
  }
}

zx_status_t FuchsiaVfs::UnmountHandle(fidl::ClientEnd<fuchsia_io::DirectoryAdmin> handle,
                                      zx::time deadline) {
  fidl::WireResult<fio::DirectoryAdmin::Unmount> result(handle, deadline.get());
  if (!result.ok()) {
    return result.status();
  }
  return result.Unwrap()->s;
}

// Installs a remote filesystem on vn and adds it to the remote_list_.
zx_status_t FuchsiaVfs::InstallRemoteLocked(fbl::RefPtr<Vnode> vn, MountChannel h) {
  if (vn == nullptr) {
    return ZX_ERR_ACCESS_DENIED;
  }

  // Allocate a node to track the remote handle
  fbl::AllocChecker ac;
  std::unique_ptr<MountNode> mount_point(new (&ac) MountNode());
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  zx_status_t status = vn->AttachRemote(std::move(h));
  if (status != ZX_OK) {
    return status;
  }
  // Save this node in the list of mounted vnodes
  mount_point->SetNode(std::move(vn));
  remote_list_.push_front(std::move(mount_point));
  return ZX_OK;
}

// Uninstall the remote filesystem mounted on vn. Removes vn from the remote_list_, and sends its
// corresponding filesystem an 'unmount' signal.
zx_status_t FuchsiaVfs::UninstallRemoteLocked(fbl::RefPtr<Vnode> vn,
                                              fidl::ClientEnd<fio::Directory>* h) {
  std::unique_ptr<MountNode> mount_point;
  {
    mount_point =
        remote_list_.erase_if([&vn](const MountNode& node) { return node.VnodeMatch(vn); });
    if (!mount_point) {
      return ZX_ERR_NOT_FOUND;
    }
  }
  *h = mount_point->ReleaseRemote();
  return ZX_OK;
}

}  // namespace fs
