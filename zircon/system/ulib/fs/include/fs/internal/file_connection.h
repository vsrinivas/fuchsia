// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_INTERNAL_FILE_CONNECTION_H_
#define FS_INTERNAL_FILE_CONNECTION_H_

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

class FileConnection final : public Connection {
 public:
  // Refer to documentation for |Connection::Connection|.
  FileConnection(fs::Vfs* vfs, fbl::RefPtr<fs::Vnode> vnode, zx::channel channel,
                 VnodeProtocol protocol, VnodeConnectionOptions options)
      : Connection(vfs, std::move(vnode), std::move(channel), protocol, options) {}

  ~FileConnection() final = default;

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
  // |fuchsia.io/File| operations.
  //

  zx_status_t Read(uint64_t count, fidl_txn_t* txn);
  zx_status_t ReadAt(uint64_t count, uint64_t offset, fidl_txn_t* txn);
  zx_status_t Write(const uint8_t* data_data, size_t data_count, fidl_txn_t* txn);
  zx_status_t WriteAt(const uint8_t* data_data, size_t data_count, uint64_t offset,
                      fidl_txn_t* txn);
  zx_status_t Seek(int64_t offset, fuchsia_io_SeekOrigin start, fidl_txn_t* txn);
  zx_status_t Truncate(uint64_t length, fidl_txn_t* txn);
  zx_status_t GetFlags(fidl_txn_t* txn);
  zx_status_t SetFlags(uint32_t flags, fidl_txn_t* txn);
  zx_status_t GetBuffer(uint32_t flags, fidl_txn_t* txn);

  constexpr static fuchsia_io_File_ops_t kOps = ([] {
    using Binder = fidl::Binder<FileConnection>;
    return fuchsia_io_File_ops_t{
        .Clone = Binder::BindMember<&FileConnection::Clone>,
        .Close = Binder::BindMember<&FileConnection::Close>,
        .Describe = Binder::BindMember<&FileConnection::Describe>,
        .Sync = Binder::BindMember<&FileConnection::Sync>,
        .GetAttr = Binder::BindMember<&FileConnection::GetAttr>,
        .SetAttr = Binder::BindMember<&FileConnection::SetAttr>,
        .NodeGetFlags = Binder::BindMember<&FileConnection::NodeGetFlags>,
        .NodeSetFlags = Binder::BindMember<&FileConnection::NodeSetFlags>,
        .Read = Binder::BindMember<&FileConnection::Read>,
        .ReadAt = Binder::BindMember<&FileConnection::ReadAt>,
        .Write = Binder::BindMember<&FileConnection::Write>,
        .WriteAt = Binder::BindMember<&FileConnection::WriteAt>,
        .Seek = Binder::BindMember<&FileConnection::Seek>,
        .Truncate = Binder::BindMember<&FileConnection::Truncate>,
        .GetFlags = Binder::BindMember<&FileConnection::GetFlags>,
        .SetFlags = Binder::BindMember<&FileConnection::SetFlags>,
        .GetBuffer = Binder::BindMember<&FileConnection::GetBuffer>,
    };
  })();

  // Current seek offset.
  size_t offset_ = 0;
};

}  // namespace internal

}  // namespace fs

#endif  // FS_INTERNAL_FILE_CONNECTION_H_
