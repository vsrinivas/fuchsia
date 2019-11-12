// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/internal/directory_connection.h>

#include <fcntl.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/io.h>
#include <lib/fdio/vfs.h>
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

namespace fs {

namespace {

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
        internal::WriteDescribeError(std::move(channel), result);
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
      // |Vfs::Open| already performs option validation for us.
      vfs->Serve(result.vnode, std::move(channel), result.validated_options);
    }
  });
}

}  // namespace

namespace internal {

zx_status_t DirectoryConnection::HandleMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  zx_status_t status = fuchsia_io_DirectoryAdmin_try_dispatch(this, txn, msg, &kOps);
  if (status != ZX_ERR_NOT_SUPPORTED) {
    return status;
  }
  return vnode()->HandleFsSpecificMessage(msg, txn);
}

zx_status_t DirectoryConnection::Clone(uint32_t clone_flags, zx_handle_t object) {
  return Connection::NodeClone(clone_flags, object);
}

zx_status_t DirectoryConnection::Close(fidl_txn_t* txn) { return Connection::NodeClose(txn); }

zx_status_t DirectoryConnection::Describe(fidl_txn_t* txn) { return Connection::NodeDescribe(txn); }

zx_status_t DirectoryConnection::Sync(fidl_txn_t* txn) { return Connection::NodeSync(txn); }

zx_status_t DirectoryConnection::GetAttr(fidl_txn_t* txn) { return Connection::NodeGetAttr(txn); }

zx_status_t DirectoryConnection::SetAttr(uint32_t flags,
                                         const fuchsia_io_NodeAttributes* attributes,
                                         fidl_txn_t* txn) {
  return Connection::NodeSetAttr(flags, attributes, txn);
}

zx_status_t DirectoryConnection::NodeGetFlags(fidl_txn_t* txn) {
  return Connection::NodeNodeGetFlags(txn);
}

zx_status_t DirectoryConnection::NodeSetFlags(uint32_t flags, fidl_txn_t* txn) {
  return Connection::NodeNodeSetFlags(flags, txn);
}

zx_status_t DirectoryConnection::Open(uint32_t open_flags, uint32_t mode, const char* path_data,
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

  FS_PRETTY_TRACE_DEBUG("[DirectoryOpen] our options: ", options(),
                        ", incoming options: ", open_options,
                        ", path: ", Path(path_data, path_size));
  if (options().flags.node_reference) {
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
  zx_status_t status = EnforceHierarchicalRights(options().rights, open_options, &open_options);
  if (status != ZX_OK) {
    FS_PRETTY_TRACE_DEBUG("Rights violation during DirectoryOpen");
    return write_error(std::move(channel), status);
  }
  OpenAt(vfs(), vnode(), std::move(channel), fbl::StringPiece(path_data, path_size), open_options,
         options().rights, mode);
  return ZX_OK;
}

zx_status_t DirectoryConnection::Unlink(const char* path_data, size_t path_size, fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryUnlink] our options: ", options(),
                        ", path: ", Path(path_data, path_size));

  if (options().flags.node_reference) {
    return fuchsia_io_DirectoryUnlink_reply(txn, ZX_ERR_BAD_HANDLE);
  }
  if (!options().rights.write) {
    return fuchsia_io_DirectoryUnlink_reply(txn, ZX_ERR_BAD_HANDLE);
  }
  zx_status_t status = vfs()->Unlink(vnode(), fbl::StringPiece(path_data, path_size));
  return fuchsia_io_DirectoryUnlink_reply(txn, status);
}

zx_status_t DirectoryConnection::ReadDirents(uint64_t max_out, fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryReadDirents] our options: ", options());

  if (options().flags.node_reference) {
    return fuchsia_io_DirectoryReadDirents_reply(txn, ZX_ERR_BAD_HANDLE, nullptr, 0);
  }
  if (max_out > ZXFIDL_MAX_MSG_BYTES) {
    return fuchsia_io_DirectoryReadDirents_reply(txn, ZX_ERR_BAD_HANDLE, nullptr, 0);
  }
  uint8_t data[max_out];
  size_t actual = 0;
  zx_status_t status = vfs()->Readdir(vnode().get(), &dircookie_, data, max_out, &actual);
  return fuchsia_io_DirectoryReadDirents_reply(txn, status, data, actual);
}

zx_status_t DirectoryConnection::Rewind(fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryRewind] our options: ", options());

  if (options().flags.node_reference) {
    return fuchsia_io_DirectoryRewind_reply(txn, ZX_ERR_BAD_HANDLE);
  }
  dircookie_.Reset();
  return fuchsia_io_DirectoryRewind_reply(txn, ZX_OK);
}

zx_status_t DirectoryConnection::GetToken(fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryGetToken] our options: ", options());

  if (!options().rights.write) {
    return fuchsia_io_DirectoryGetToken_reply(txn, ZX_ERR_BAD_HANDLE, ZX_HANDLE_INVALID);
  }
  zx::event returned_token;
  zx_status_t status = vfs()->VnodeToToken(vnode(), &token(), &returned_token);
  return fuchsia_io_DirectoryGetToken_reply(txn, status, returned_token.release());
}

zx_status_t DirectoryConnection::Rename(const char* src_data, size_t src_size,
                                        zx_handle_t dst_parent_token, const char* dst_data,
                                        size_t dst_size, fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryRename] our options: ", options(),
                        ", src: ", Path(src_data, src_size), ", dst: ", Path(dst_data, dst_size));

  zx::event token(dst_parent_token);
  fbl::StringPiece oldStr(src_data, src_size);
  fbl::StringPiece newStr(dst_data, dst_size);

  if (src_size < 1 || dst_size < 1) {
    return fuchsia_io_DirectoryRename_reply(txn, ZX_ERR_INVALID_ARGS);
  }
  if (options().flags.node_reference) {
    return fuchsia_io_DirectoryRename_reply(txn, ZX_ERR_BAD_HANDLE);
  }
  if (!options().rights.write) {
    return fuchsia_io_DirectoryRename_reply(txn, ZX_ERR_BAD_HANDLE);
  }
  zx_status_t status =
      vfs()->Rename(std::move(token), vnode(), std::move(oldStr), std::move(newStr));
  return fuchsia_io_DirectoryRename_reply(txn, status);
}

zx_status_t DirectoryConnection::Link(const char* src_data, size_t src_size,
                                      zx_handle_t dst_parent_token, const char* dst_data,
                                      size_t dst_size, fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryLink] our options: ", options(),
                        ", src: ", Path(src_data, src_size), ", dst: ", Path(dst_data, dst_size));

  zx::event token(dst_parent_token);
  fbl::StringPiece oldStr(src_data, src_size);
  fbl::StringPiece newStr(dst_data, dst_size);

  if (src_size < 1 || dst_size < 1) {
    return fuchsia_io_DirectoryLink_reply(txn, ZX_ERR_INVALID_ARGS);
  }
  if (options().flags.node_reference) {
    return fuchsia_io_DirectoryLink_reply(txn, ZX_ERR_BAD_HANDLE);
  }
  if (!options().rights.write) {
    return fuchsia_io_DirectoryLink_reply(txn, ZX_ERR_BAD_HANDLE);
  }
  zx_status_t status = vfs()->Link(std::move(token), vnode(), std::move(oldStr), std::move(newStr));
  return fuchsia_io_DirectoryLink_reply(txn, status);
}

zx_status_t DirectoryConnection::Watch(uint32_t mask, uint32_t watch_options, zx_handle_t handle,
                                       fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryWatch] our options: ", options());

  if (options().flags.node_reference) {
    return fuchsia_io_DirectoryWatch_reply(txn, ZX_ERR_BAD_HANDLE);
  }
  zx::channel watcher(handle);
  zx_status_t status = vnode()->WatchDir(vfs(), mask, watch_options, std::move(watcher));
  return fuchsia_io_DirectoryWatch_reply(txn, status);
}

zx_status_t DirectoryConnection::Mount(zx_handle_t remote, fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryAdminMount] our options: ", options());

  if (!options().rights.admin) {
    vfs_unmount_handle(remote, 0);
    return fuchsia_io_DirectoryAdminMount_reply(txn, ZX_ERR_ACCESS_DENIED);
  }
  MountChannel c = MountChannel(remote);
  zx_status_t status = vfs()->InstallRemote(vnode(), std::move(c));
  return fuchsia_io_DirectoryAdminMount_reply(txn, status);
}

zx_status_t DirectoryConnection::MountAndCreate(zx_handle_t remote, const char* name,
                                                size_t name_size, uint32_t flags, fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryAdminMountAndCreate] our options: ", options());

  if (!options().rights.admin) {
    vfs_unmount_handle(remote, 0);
    return fuchsia_io_DirectoryAdminMount_reply(txn, ZX_ERR_ACCESS_DENIED);
  }
  fbl::StringPiece str(name, name_size);
  zx_status_t status = vfs()->MountMkdir(vnode(), std::move(str), MountChannel(remote), flags);
  return fuchsia_io_DirectoryAdminMount_reply(txn, status);
}

zx_status_t DirectoryConnection::Unmount(fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryAdminUnmount] our options: ", options());

  if (!options().rights.admin) {
    return fuchsia_io_DirectoryAdminUnmount_reply(txn, ZX_ERR_ACCESS_DENIED);
  }
  return Connection::UnmountAndShutdown(txn);
}

zx_status_t DirectoryConnection::UnmountNode(fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryAdminUnmountNode] our options: ", options());

  if (!options().rights.admin) {
    return fuchsia_io_DirectoryAdminUnmountNode_reply(txn, ZX_ERR_ACCESS_DENIED, ZX_HANDLE_INVALID);
  }
  zx::channel c;
  zx_status_t status = vfs()->UninstallRemote(vnode(), &c);
  return fuchsia_io_DirectoryAdminUnmountNode_reply(txn, status, c.release());
}

zx_status_t DirectoryConnection::QueryFilesystem(fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryAdminQueryFilesystem] our options: ", options());

  fuchsia_io_FilesystemInfo info;
  zx_status_t status = vnode()->QueryFilesystem(&info);
  return fuchsia_io_DirectoryAdminQueryFilesystem_reply(txn, status,
                                                        status == ZX_OK ? &info : nullptr);
}

zx_status_t DirectoryConnection::GetDevicePath(fidl_txn_t* txn) {
  FS_PRETTY_TRACE_DEBUG("[DirectoryAdminGetDevicePath] our options: ", options());

  if (!options().rights.admin) {
    return fuchsia_io_DirectoryAdminGetDevicePath_reply(txn, ZX_ERR_ACCESS_DENIED, nullptr, 0);
  }

  char name[fuchsia_io_MAX_PATH];
  size_t actual = 0;
  zx_status_t status = vnode()->GetDevicePath(sizeof(name), name, &actual);
  return fuchsia_io_DirectoryAdminGetDevicePath_reply(txn, status, name, actual);
}

}  // namespace internal

}  // namespace fs
