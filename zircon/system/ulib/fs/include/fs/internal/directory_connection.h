// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_INTERNAL_DIRECTORY_CONNECTION_H_
#define FS_INTERNAL_DIRECTORY_CONNECTION_H_

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <fuchsia/io/c/fidl.h>
#include <lib/fidl-utils/bind.h>

#include <fs/internal/connection.h>
#include <fs/vfs.h>
#include <fs/vfs_types.h>
#include <fs/vnode.h>

namespace fs {

namespace internal {

class DirectoryConnection final : public Connection {
 public:
  // Refer to documentation for |Connection::Connection|.
  DirectoryConnection(fs::Vfs* vfs, fbl::RefPtr<fs::Vnode> vnode, zx::channel channel,
                      VnodeProtocol protocol, VnodeConnectionOptions options)
      : Connection(vfs, std::move(vnode), std::move(channel), protocol, options) {}

  ~DirectoryConnection() final = default;

 private:
  zx_status_t HandleMessage(fidl_msg_t* msg, fidl_txn_t* txn) final;

  //
  // |fuchsia.io/Node| operations.
  //

  zx_status_t Clone(uint32_t flags, zx_handle_t object);
  zx_status_t Close(fidl_txn_t* txn);
  zx_status_t Describe(fidl_txn_t* txn);
  zx_status_t Sync(fidl_txn_t* txn);
  zx_status_t GetAttr(fidl_txn_t* txn);
  zx_status_t SetAttr(uint32_t flags, const fuchsia_io_NodeAttributes* attributes,
                      fidl_txn_t* txn);
  zx_status_t NodeGetFlags(fidl_txn_t* txn);
  zx_status_t NodeSetFlags(uint32_t flags, fidl_txn_t* txn);

  //
  // |fuchsia.io/Directory| operations.
  //

  zx_status_t Open(uint32_t flags, uint32_t mode, const char* path_data, size_t path_size,
                   zx_handle_t object);
  zx_status_t Unlink(const char* path_data, size_t path_size, fidl_txn_t* txn);
  zx_status_t ReadDirents(uint64_t max_out, fidl_txn_t* txn);
  zx_status_t Rewind(fidl_txn_t* txn);
  zx_status_t GetToken(fidl_txn_t* txn);
  zx_status_t Rename(const char* src_data, size_t src_size, zx_handle_t dst_parent_token,
                     const char* dst_data, size_t dst_size, fidl_txn_t* txn);
  zx_status_t Link(const char* src_data, size_t src_size, zx_handle_t dst_parent_token,
                   const char* dst_data, size_t dst_size, fidl_txn_t* txn);
  zx_status_t Watch(uint32_t mask, uint32_t options, zx_handle_t watcher, fidl_txn_t* txn);

  //
  // |fuchsia.io/DirectoryAdmin| operations.
  //

  zx_status_t Mount(zx_handle_t remote, fidl_txn_t* txn);
  zx_status_t MountAndCreate(zx_handle_t remote, const char* name, size_t name_size,
                             uint32_t flags, fidl_txn_t* txn);
  zx_status_t Unmount(fidl_txn_t* txn);
  zx_status_t UnmountNode(fidl_txn_t* txn);
  zx_status_t QueryFilesystem(fidl_txn_t* txn);
  zx_status_t GetDevicePath(fidl_txn_t* txn);

  constexpr static fuchsia_io_DirectoryAdmin_ops_t kOps = ([] {
    using Binder = fidl::Binder<DirectoryConnection>;
    return fuchsia_io_DirectoryAdmin_ops_t{
        .Clone = Binder::BindMember<&DirectoryConnection::Clone>,
        .Close = Binder::BindMember<&DirectoryConnection::Close>,
        .Describe = Binder::BindMember<&DirectoryConnection::Describe>,
        .Sync = Binder::BindMember<&DirectoryConnection::Sync>,
        .GetAttr = Binder::BindMember<&DirectoryConnection::GetAttr>,
        .SetAttr = Binder::BindMember<&DirectoryConnection::SetAttr>,
        .NodeGetFlags = Binder::BindMember<&DirectoryConnection::NodeGetFlags>,
        .NodeSetFlags = Binder::BindMember<&DirectoryConnection::NodeSetFlags>,
        .Open = Binder::BindMember<&DirectoryConnection::Open>,
        .Unlink = Binder::BindMember<&DirectoryConnection::Unlink>,
        .ReadDirents = Binder::BindMember<&DirectoryConnection::ReadDirents>,
        .Rewind = Binder::BindMember<&DirectoryConnection::Rewind>,
        .GetToken = Binder::BindMember<&DirectoryConnection::GetToken>,
        .Rename = Binder::BindMember<&DirectoryConnection::Rename>,
        .Link = Binder::BindMember<&DirectoryConnection::Link>,
        .Watch = Binder::BindMember<&DirectoryConnection::Watch>,
        .Mount = Binder::BindMember<&DirectoryConnection::Mount>,
        .MountAndCreate = Binder::BindMember<&DirectoryConnection::MountAndCreate>,
        .Unmount = Binder::BindMember<&DirectoryConnection::Unmount>,
        .UnmountNode = Binder::BindMember<&DirectoryConnection::UnmountNode>,
        .QueryFilesystem = Binder::BindMember<&DirectoryConnection::QueryFilesystem>,
        .GetDevicePath = Binder::BindMember<&DirectoryConnection::GetDevicePath>,
    };
  })();

  // Directory cookie for readdir operations.
  fs::vdircookie_t dircookie_{};
};

}  // namespace internal

}  // namespace fs

#endif  // FS_INTERNAL_DIRECTORY_CONNECTION_H_
