// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_DIRECTORY_CONNECTION_H_
#define SRC_LIB_STORAGE_VFS_CPP_DIRECTORY_CONNECTION_H_

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include "src/lib/storage/vfs/cpp/connection.h"
#include "src/lib/storage/vfs/cpp/vfs.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

namespace fs {

namespace internal {

class DirectoryConnection final : public Connection,
                                  public fidl::WireServer<fuchsia_io::DirectoryAdmin> {
 public:
  // Refer to documentation for |Connection::Connection|.
  DirectoryConnection(fs::FuchsiaVfs* vfs, fbl::RefPtr<fs::Vnode> vnode, VnodeProtocol protocol,
                      VnodeConnectionOptions options);

  ~DirectoryConnection() final = default;

 protected:
  void OnTeardown();

 private:
  //
  // |fuchsia.io/Node| operations.
  //

  void Clone(CloneRequestView request, CloneCompleter::Sync& completer) final;
  void Close(CloseRequestView request, CloseCompleter::Sync& completer) final;
  void Describe(DescribeRequestView request, DescribeCompleter::Sync& completer) final;
  void Sync(SyncRequestView request, SyncCompleter::Sync& completer) final;
  void GetAttr(GetAttrRequestView request, GetAttrCompleter::Sync& completer) final;
  void SetAttr(SetAttrRequestView request, SetAttrCompleter::Sync& completer) final;
  void NodeGetFlags(NodeGetFlagsRequestView request, NodeGetFlagsCompleter::Sync& completer) final;
  void NodeSetFlags(NodeSetFlagsRequestView request, NodeSetFlagsCompleter::Sync& completer) final;

  //
  // |fuchsia.io/Directory| operations.
  //

  void Open(OpenRequestView request, OpenCompleter::Sync& completer) final;
  void Unlink(UnlinkRequestView request, UnlinkCompleter::Sync& completer) final;
  void Unlink2(Unlink2RequestView request, Unlink2Completer::Sync& completer) final;
  void ReadDirents(ReadDirentsRequestView request, ReadDirentsCompleter::Sync& completer) final;
  void Rewind(RewindRequestView request, RewindCompleter::Sync& completer) final;
  void GetToken(GetTokenRequestView request, GetTokenCompleter::Sync& completer) final;
  void Rename(RenameRequestView request, RenameCompleter::Sync& completer) final;
  void Rename2(Rename2RequestView request, Rename2Completer::Sync& completer) final;
  void Link(LinkRequestView request, LinkCompleter::Sync& completer) final;
  void Watch(WatchRequestView request, WatchCompleter::Sync& completer) final;
  void AddInotifyFilter(AddInotifyFilterRequestView request,
                        AddInotifyFilterCompleter::Sync& completer) final;

  //
  // |fuchsia.io/DirectoryAdmin| operations.
  //

  void Mount(MountRequestView request, MountCompleter::Sync& completer) final;
  void MountAndCreate(MountAndCreateRequestView request,
                      MountAndCreateCompleter::Sync& completer) final;
  void Unmount(UnmountRequestView request, UnmountCompleter::Sync& completer) final;
  void UnmountNode(UnmountNodeRequestView request, UnmountNodeCompleter::Sync& completer) final;
  void QueryFilesystem(QueryFilesystemRequestView request,
                       QueryFilesystemCompleter::Sync& completer) final;
  void GetDevicePath(GetDevicePathRequestView request,
                     GetDevicePathCompleter::Sync& completer) final;

  //
  // |fuchsia.io2/AdvisoryLocking| operations.
  //

  void AdvisoryLock(AdvisoryLockRequestView request, AdvisoryLockCompleter::Sync& _completer) final;

  // Directory cookie for readdir operations.
  fs::VdirCookie dircookie_;
};

}  // namespace internal

}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_DIRECTORY_CONNECTION_H_
