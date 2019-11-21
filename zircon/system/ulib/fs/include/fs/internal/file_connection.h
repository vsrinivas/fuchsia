// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_INTERNAL_FILE_CONNECTION_H_
#define FS_INTERNAL_FILE_CONNECTION_H_

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fidl-utils/bind.h>

#include <fs/internal/connection.h>
#include <fs/vfs.h>
#include <fs/vfs_types.h>
#include <fs/vnode.h>

namespace fs {

namespace internal {

class FileConnection final : public Connection, public llcpp::fuchsia::io::File::Interface {
 public:
  // Refer to documentation for |Connection::Connection|.
  FileConnection(fs::Vfs* vfs, fbl::RefPtr<fs::Vnode> vnode, VnodeProtocol protocol,
                 VnodeConnectionOptions options)
      : Connection(vfs, std::move(vnode), protocol, options) {}

  ~FileConnection() final = default;

 private:
  void HandleMessage(fidl_msg_t* msg, FidlTransaction* txn) final;

  //
  // |fuchsia.io/Node| operations.
  //

  void Clone(uint32_t flags, zx::channel object, CloneCompleter::Sync completer) final;
  void Close(CloseCompleter::Sync completer) final;
  void Describe(DescribeCompleter::Sync completer) final;
  void Sync(SyncCompleter::Sync completer) final;
  void GetAttr(GetAttrCompleter::Sync completer) final;
  void SetAttr(uint32_t flags, llcpp::fuchsia::io::NodeAttributes attributes,
               SetAttrCompleter::Sync completer) final;
  void NodeGetFlags(NodeGetFlagsCompleter::Sync completer) final;
  void NodeSetFlags(uint32_t flags, NodeSetFlagsCompleter::Sync completer) final;

  //
  // |fuchsia.io/File| operations.
  //

  void Read(uint64_t count, ReadCompleter::Sync completer) final;
  void ReadAt(uint64_t count, uint64_t offset, ReadAtCompleter::Sync completer) final;
  void Write(fidl::VectorView<uint8_t> data, WriteCompleter::Sync completer) final;
  void WriteAt(fidl::VectorView<uint8_t> data, uint64_t offset,
               WriteAtCompleter::Sync completer) final;
  void Seek(int64_t offset, llcpp::fuchsia::io::SeekOrigin start,
            SeekCompleter::Sync completer) final;
  void Truncate(uint64_t length, TruncateCompleter::Sync completer) final;
  void GetFlags(GetFlagsCompleter::Sync completer) final;
  void SetFlags(uint32_t flags, SetFlagsCompleter::Sync completer) final;
  void GetBuffer(uint32_t flags, GetBufferCompleter::Sync completer) final;

  // Current seek offset.
  size_t offset_ = 0;
};

}  // namespace internal

}  // namespace fs

#endif  // FS_INTERNAL_FILE_CONNECTION_H_
