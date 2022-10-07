// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/directory_connection.h"

#include <fcntl.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/io.h>
#include <lib/fdio/vfs.h>
#include <lib/zx/handle.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zircon/assert.h>

#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

#include <fbl/string_buffer.h>

#include "src/lib/storage/vfs/cpp/advisory_lock.h"
#include "src/lib/storage/vfs/cpp/debug.h"
#include "src/lib/storage/vfs/cpp/fidl_transaction.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

namespace fio = fuchsia_io;

namespace fs {

namespace {

// Performs a path walk and opens a connection to another node.
void OpenAt(FuchsiaVfs* vfs, const fbl::RefPtr<Vnode>& parent,
            fidl::ServerEnd<fio::Node> server_end, std::string_view path,
            VnodeConnectionOptions options, Rights parent_rights, uint32_t mode) {
  vfs->Open(parent, path, options, parent_rights, mode)
      .visit([vfs, &server_end, options, mode](auto&& result) {
        using ResultT = std::decay_t<decltype(result)>;
        using OpenResult = fs::Vfs::OpenResult;
        if constexpr (std::is_same_v<ResultT, OpenResult::Error>) {
          if (options.flags.describe) {
            // Ignore errors since there is nothing we can do if this fails.
            [[maybe_unused]] const fidl::Status unused_result =
                fidl::WireSendEvent(server_end)->OnOpen(result, fio::wire::NodeInfoDeprecated());
            server_end.reset();
          }
        } else if constexpr (std::is_same_v<ResultT, OpenResult::Remote>) {
          const fbl::RefPtr<const Vnode> vn = result.vnode;
          const std::string_view path = result.path;

          // Ignore errors since there is nothing we can do if this fails.
          [[maybe_unused]] const zx_status_t status =
              vn->OpenRemote(options.ToIoV1Flags(), mode, fidl::StringView::FromExternal(path),
                             std::move(server_end));
        } else if constexpr (std::is_same_v<ResultT, OpenResult::Ok>) {
          // |Vfs::Open| already performs option validation for us.
          vfs->Serve(result.vnode, server_end.TakeChannel(), result.validated_options);
        }
      });
}

// Performs a path walk and adds inotify filter to the obtained vnode.
void AddInotifyFilterAt(FuchsiaVfs* vfs, const fbl::RefPtr<Vnode>& parent, std::string_view path,
                        fio::wire::InotifyWatchMask filter, uint32_t watch_descriptor,
                        zx::socket socket) {
  // TODO Not handling remote handoff currently.
  vfs->TraversePathFetchVnode(parent, path).visit([&](auto&& result) {
    using ResultT = std::decay_t<decltype(result)>;
    using TraversePathResult = fs::Vfs::TraversePathResult;
    if constexpr (std::is_same_v<ResultT, TraversePathResult::Error>) {
      return;
    } else if constexpr (std::is_same_v<ResultT, TraversePathResult::Remote>) {
      // Remote handoff to a remote filesystem node.
      // TODO remote handoffs not supported for inotify currently.
      return;
    } else if constexpr (std::is_same_v<ResultT, TraversePathResult::Ok>) {
      // We have got the vnode to add the filter to.
      vfs->AddInotifyFilterToVnode(result.vnode, parent, filter, watch_descriptor,
                                   std::move(socket));
    }
  });
}
}  // namespace

namespace internal {

DirectoryConnection::DirectoryConnection(fs::FuchsiaVfs* vfs, fbl::RefPtr<fs::Vnode> vnode,
                                         VnodeProtocol protocol, VnodeConnectionOptions options)
    : Connection(vfs, std::move(vnode), protocol, options,
                 FidlProtocol::Create<fuchsia_io::Directory>(this)) {}

void DirectoryConnection::Clone(CloneRequestView request, CloneCompleter::Sync& completer) {
  // TODO(https://fxbug.dev/111302): test this.
  Connection::NodeClone(request->flags | fio::OpenFlags::kDirectory, std::move(request->object));
}

void DirectoryConnection::Close(CloseCompleter::Sync& completer) {
  zx::status result = Connection::NodeClose();
  if (result.is_error()) {
    completer.ReplyError(result.status_value());
  } else {
    completer.ReplySuccess();
  }
}

void DirectoryConnection::Query(QueryCompleter::Sync& completer) {
  completer.Reply(Connection::NodeQuery());
}

void DirectoryConnection::DescribeDeprecated(DescribeDeprecatedCompleter::Sync& completer) {
  zx::status result = Connection::NodeDescribe();
  if (result.is_error()) {
    completer.Close(result.status_value());
    return;
  }
  ConvertToIoV1NodeInfo(std::move(result).value(), [&](fio::wire::NodeInfoDeprecated&& info) {
    completer.Reply(std::move(info));
  });
}

void DirectoryConnection::GetConnectionInfo(GetConnectionInfoCompleter::Sync& completer) {
  completer.Reply({});
}

void DirectoryConnection::Sync(SyncCompleter::Sync& completer) {
  Connection::NodeSync([completer = completer.ToAsync()](zx_status_t sync_status) mutable {
    if (sync_status != ZX_OK) {
      completer.ReplyError(sync_status);
    } else {
      completer.ReplySuccess();
    }
  });
}

void DirectoryConnection::GetAttr(GetAttrCompleter::Sync& completer) {
  zx::status result = Connection::NodeGetAttr();
  if (result.is_error()) {
    completer.Reply(result.status_value(), fio::wire::NodeAttributes());
  } else {
    completer.Reply(ZX_OK, result.value().ToIoV1NodeAttributes());
  }
}

void DirectoryConnection::SetAttr(SetAttrRequestView request, SetAttrCompleter::Sync& completer) {
  zx::status result = Connection::NodeSetAttr(request->flags, request->attributes);
  if (result.is_error()) {
    completer.Reply(result.status_value());
  } else {
    completer.Reply(ZX_OK);
  }
}

void DirectoryConnection::GetFlags(GetFlagsCompleter::Sync& completer) {
  zx::status result = Connection::NodeGetFlags();
  if (result.is_error()) {
    completer.Reply(result.status_value(), {});
  } else {
    completer.Reply(ZX_OK, result.value());
  }
}

void DirectoryConnection::SetFlags(SetFlagsRequestView request,
                                   SetFlagsCompleter::Sync& completer) {
  zx::status result = Connection::NodeSetFlags(request->flags);
  if (result.is_error()) {
    completer.Reply(result.status_value());
  } else {
    completer.Reply(ZX_OK);
  }
}

void DirectoryConnection::AddInotifyFilter(AddInotifyFilterRequestView request,
                                           AddInotifyFilterCompleter::Sync& completer) {
  AddInotifyFilterAt(vfs(), vnode(), std::string_view(request->path.data(), request->path.size()),
                     request->filter, request->watch_descriptor, std::move(request->socket));
  completer.Reply();
}

void DirectoryConnection::Open(OpenRequestView request, OpenCompleter::Sync& completer) {
  auto write_error = [describe = request->flags & fio::wire::OpenFlags::kDescribe](
                         fidl::ServerEnd<fio::Node> channel, zx_status_t error) {
    if (describe) {
      // Ignore errors since there is nothing we can do if this fails.
      [[maybe_unused]] auto result =
          fidl::WireSendEvent(channel)->OnOpen(error, fio::wire::NodeInfoDeprecated());
      channel.reset();
    }
  };

  std::string_view path(request->path.data(), request->path.size());
  if (path.size() > PATH_MAX) {
    return write_error(std::move(request->object), ZX_ERR_BAD_PATH);
  }

  if (path.empty() ||
      ((path == "." || path == "/") && (request->flags & fio::wire::OpenFlags::kNotDirectory))) {
    return write_error(std::move(request->object), ZX_ERR_INVALID_ARGS);
  }

  fio::wire::OpenFlags flags = request->flags;
  if (path.back() == '/') {
    flags |= fio::wire::OpenFlags::kDirectory;
  }

  auto open_options = VnodeConnectionOptions::FromIoV1Flags(flags);

  if (!PrevalidateFlags(flags)) {
    FS_PRETTY_TRACE_DEBUG("[DirectoryOpen] prevalidate failed",
                          ", incoming flags: ", request->flags, ", path: ", request->path);
    return write_error(std::move(request->object), ZX_ERR_INVALID_ARGS);
  }

  uint32_t mode = request->mode;
  const uint32_t mode_type = mode & fio::wire::kModeTypeMask;
  if (mode_type == 0) {
    if (open_options.flags.directory) {
      mode |= fio::wire::kModeTypeDirectory;
    }
  } else {
    if (open_options.flags.directory && mode_type != fio::wire::kModeTypeDirectory) {
      return write_error(std::move(request->object), ZX_ERR_INVALID_ARGS);
    }

    if (open_options.flags.not_directory && mode_type == fio::wire::kModeTypeDirectory) {
      return write_error(std::move(request->object), ZX_ERR_INVALID_ARGS);
    }
  }

  FS_PRETTY_TRACE_DEBUG("[DirectoryOpen] our options: ", options(),
                        ", incoming options: ", open_options, ", path: ", request->path);
  if (options().flags.node_reference) {
    return write_error(std::move(request->object), ZX_ERR_BAD_HANDLE);
  }
  if (open_options.flags.clone_same_rights) {
    return write_error(std::move(request->object), ZX_ERR_INVALID_ARGS);
  }

  // Check for directory rights inheritance
  if (!vnode()->IsSkipRightsEnforcementDevfsOnlyDoNotUse()) {
    zx_status_t status = EnforceHierarchicalRights(options().rights, open_options, &open_options);
    if (status != ZX_OK) {
      FS_PRETTY_TRACE_DEBUG("Rights violation during DirectoryOpen");
      return write_error(std::move(request->object), status);
    }
  }
  OpenAt(vfs(), vnode(), std::move(request->object), path, open_options, options().rights, mode);
}

void DirectoryConnection::Unlink(UnlinkRequestView request, UnlinkCompleter::Sync& completer) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryUnlink] our options: ", options(), ", name: ", request->name);

  if (options().flags.node_reference) {
    completer.ReplyError(ZX_ERR_BAD_HANDLE);
    return;
  }
  if (!options().rights.write) {
    completer.ReplyError(ZX_ERR_BAD_HANDLE);
    return;
  }
  std::string_view name_str(request->name.data(), request->name.size());
  if (!IsValidName(name_str)) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }
  zx_status_t status =
      vfs()->Unlink(vnode(), name_str,
                    request->options.has_flags() &&
                        static_cast<bool>((request->options.flags() &
                                           fuchsia_io::wire::UnlinkFlags::kMustBeDirectory)));
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void DirectoryConnection::ReadDirents(ReadDirentsRequestView request,
                                      ReadDirentsCompleter::Sync& completer) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryReadDirents] our options: ", options());

  if (options().flags.node_reference) {
    completer.Reply(ZX_ERR_BAD_HANDLE, fidl::VectorView<uint8_t>());
    return;
  }
  if (request->max_bytes > fio::wire::kMaxBuf) {
    completer.Reply(ZX_ERR_BAD_HANDLE, fidl::VectorView<uint8_t>());
    return;
  }
  uint8_t data[request->max_bytes];
  size_t actual = 0;
  zx_status_t status =
      vfs()->Readdir(vnode().get(), &dircookie_, data, request->max_bytes, &actual);
  completer.Reply(status, fidl::VectorView<uint8_t>::FromExternal(data, actual));
}

void DirectoryConnection::Rewind(RewindCompleter::Sync& completer) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryRewind] our options: ", options());

  if (options().flags.node_reference) {
    completer.Reply(ZX_ERR_BAD_HANDLE);
    return;
  }
  dircookie_ = VdirCookie();
  completer.Reply(ZX_OK);
}

void DirectoryConnection::GetToken(GetTokenCompleter::Sync& completer) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryGetToken] our options: ", options());

  if (!options().rights.write) {
    completer.Reply(ZX_ERR_BAD_HANDLE, zx::handle());
    return;
  }
  zx::event returned_token;
  zx_status_t status = vfs()->VnodeToToken(vnode(), &token(), &returned_token);
  completer.Reply(status, std::move(returned_token));
}

void DirectoryConnection::Rename(RenameRequestView request, RenameCompleter::Sync& completer) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryRename] our options: ", options(), ", src: ", request->src,
                        ", dst: ", request->dst);

  if (request->src.empty() || request->dst.empty()) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }
  if (options().flags.node_reference) {
    completer.ReplyError(ZX_ERR_BAD_HANDLE);
    return;
  }
  if (!options().rights.write) {
    completer.ReplyError(ZX_ERR_BAD_HANDLE);
    return;
  }
  zx_status_t status = vfs()->Rename(std::move(request->dst_parent_token), vnode(),
                                     std::string_view(request->src.data(), request->src.size()),
                                     std::string_view(request->dst.data(), request->dst.size()));
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void DirectoryConnection::Link(LinkRequestView request, LinkCompleter::Sync& completer) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryLink] our options: ", options(), ", src: ", request->src,
                        ", dst: ", request->dst);

  // |fuchsia.io/Directory.Rename| only specified the token to be a generic handle; casting it here.
  zx::event token(request->dst_parent_token.release());

  if (request->src.empty() || request->dst.empty()) {
    completer.Reply(ZX_ERR_INVALID_ARGS);
    return;
  }
  if (options().flags.node_reference) {
    completer.Reply(ZX_ERR_BAD_HANDLE);
    return;
  }
  if (!options().rights.write) {
    completer.Reply(ZX_ERR_BAD_HANDLE);
    return;
  }
  zx_status_t status = vfs()->Link(std::move(token), vnode(),
                                   std::string_view(request->src.data(), request->src.size()),
                                   std::string_view(request->dst.data(), request->dst.size()));
  completer.Reply(status);
}

void DirectoryConnection::Watch(WatchRequestView request, WatchCompleter::Sync& completer) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryWatch] our options: ", options());

  if (options().flags.node_reference) {
    completer.Reply(ZX_ERR_BAD_HANDLE);
    return;
  }
  zx_status_t status =
      vnode()->WatchDir(vfs(), request->mask, request->options, std::move(request->watcher));
  completer.Reply(status);
}

void DirectoryConnection::QueryFilesystem(QueryFilesystemCompleter::Sync& completer) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryQueryFilesystem] our options: ", options());

  fuchsia_io::wire::FilesystemInfo info;
  zx_status_t status = vnode()->QueryFilesystem(&info);
  completer.Reply(status,
                  status == ZX_OK
                      ? fidl::ObjectView<fuchsia_io::wire::FilesystemInfo>::FromExternal(&info)
                      : nullptr);
}

void DirectoryConnection::AdvisoryLock(AdvisoryLockRequestView request,
                                       AdvisoryLockCompleter::Sync& completer) {
  zx_koid_t owner = GetChannelOwnerKoid();
  // advisory_lock replies to the completer
  auto async_completer = completer.ToAsync();
  fit::callback<void(zx_status_t)> callback = file_lock::lock_completer_t(
      [lock_completer = std::move(async_completer)](zx_status_t status) mutable {
        lock_completer.ReplyError(status);
      });

  advisory_lock(owner, vnode(), false, request->request, std::move(callback));
}
void DirectoryConnection::OnTeardown() {
  auto owner = GetChannelOwnerKoid();
  vnode()->DeleteFileLockInTeardown(owner);
}

}  // namespace internal

}  // namespace fs
