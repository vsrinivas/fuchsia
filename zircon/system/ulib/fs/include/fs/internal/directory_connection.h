// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_INTERNAL_DIRECTORY_CONNECTION_H_
#define FS_INTERNAL_DIRECTORY_CONNECTION_H_

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <fuchsia/io/llcpp/fidl.h>

#include <fs/internal/connection.h>
#include <fs/vfs.h>
#include <fs/vfs_types.h>
#include <fs/vnode.h>

namespace fs {

namespace internal {

class DirectoryConnection final : public Connection,
                                  public llcpp::fuchsia::io::DirectoryAdmin::Interface {
 public:
  // Refer to documentation for |Connection::Connection|.
  DirectoryConnection(fs::Vfs* vfs, fbl::RefPtr<fs::Vnode> vnode, VnodeProtocol protocol,
                      VnodeConnectionOptions options);

  ~DirectoryConnection() final = default;

 private:

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
  // |fuchsia.io/Directory| operations.
  //

  void Open(uint32_t flags, uint32_t mode, fidl::StringView path, zx::channel object,
            OpenCompleter::Sync completer) final;
  void Unlink(fidl::StringView path, UnlinkCompleter::Sync completer) final;
  void ReadDirents(uint64_t max_out, ReadDirentsCompleter::Sync completer) final;
  void Rewind(RewindCompleter::Sync completer) final;
  void GetToken(GetTokenCompleter::Sync completer) final;
  void Rename(fidl::StringView src, zx::handle dst_parent_token, fidl::StringView dst,
              RenameCompleter::Sync completer) final;
  void Link(fidl::StringView src, zx::handle dst_parent_token, fidl::StringView dst,
            LinkCompleter::Sync completer) final;
  void Watch(uint32_t mask, uint32_t options, zx::channel watcher,
             WatchCompleter::Sync completer) final;

  //
  // |fuchsia.io/DirectoryAdmin| operations.
  //

  void Mount(zx::channel remote, MountCompleter::Sync completer) final;
  void MountAndCreate(zx::channel remote, fidl::StringView name, uint32_t flags,
                      MountAndCreateCompleter::Sync completer) final;
  void Unmount(UnmountCompleter::Sync completer) final;
  void UnmountNode(UnmountNodeCompleter::Sync completer) final;
  void QueryFilesystem(QueryFilesystemCompleter::Sync completer) final;
  void GetDevicePath(GetDevicePathCompleter::Sync completer) final;

  // Directory cookie for readdir operations.
  fs::vdircookie_t dircookie_{};
};

}  // namespace internal

}  // namespace fs

#endif  // FS_INTERNAL_DIRECTORY_CONNECTION_H_
