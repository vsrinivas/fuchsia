// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_NODE_CONNECTION_H_
#define SRC_LIB_STORAGE_VFS_CPP_NODE_CONNECTION_H_

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <fidl/fuchsia.io/cpp/wire.h>

#include "src/lib/storage/vfs/cpp/connection.h"
#include "src/lib/storage/vfs/cpp/vfs.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

namespace fs {

namespace internal {

class NodeConnection final : public Connection, public fidl::WireServer<fuchsia_io::Node> {
 public:
  // Refer to documentation for |Connection::Connection|.
  NodeConnection(fs::FuchsiaVfs* vfs, fbl::RefPtr<fs::Vnode> vnode, VnodeProtocol protocol,
                 VnodeConnectionOptions options);

  ~NodeConnection() final = default;

 private:
  //
  // |fuchsia.io/Node| operations.
  //

  void Clone(CloneRequestView request, CloneCompleter::Sync& completer) final;
  void CloseDeprecated(CloseDeprecatedRequestView request,
                       CloseDeprecatedCompleter::Sync& completer) final;
  void Close(CloseRequestView request, CloseCompleter::Sync& completer) final;
  void Describe(DescribeRequestView request, DescribeCompleter::Sync& completer) final;
  void Describe2(Describe2RequestView request, Describe2Completer::Sync& completer) final;
  void SyncDeprecated(SyncDeprecatedRequestView request,
                      SyncDeprecatedCompleter::Sync& completer) final;
  void Sync(SyncRequestView request, SyncCompleter::Sync& completer) final;
  void GetAttr(GetAttrRequestView request, GetAttrCompleter::Sync& completer) final;
  void SetAttr(SetAttrRequestView request, SetAttrCompleter::Sync& completer) final;
  void GetFlags(GetFlagsRequestView request, GetFlagsCompleter::Sync& completer) final;
  void SetFlags(SetFlagsRequestView request, SetFlagsCompleter::Sync& completer) final;
  void QueryFilesystem(QueryFilesystemRequestView request,
                       QueryFilesystemCompleter::Sync& completer) final;
};

}  // namespace internal

}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_NODE_CONNECTION_H_
