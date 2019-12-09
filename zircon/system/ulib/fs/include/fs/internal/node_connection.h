// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_INTERNAL_NODE_CONNECTION_H_
#define FS_INTERNAL_NODE_CONNECTION_H_

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

class NodeConnection final : public Connection, public llcpp::fuchsia::io::Node::Interface {
 public:
  // Refer to documentation for |Connection::Connection|.
  NodeConnection(fs::Vfs* vfs, fbl::RefPtr<fs::Vnode> vnode, VnodeProtocol protocol,
                 VnodeConnectionOptions options)
      : Connection(vfs, std::move(vnode), protocol, options) {}

  ~NodeConnection() final = default;

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
};

}  // namespace internal

}  // namespace fs

#endif  // FS_INTERNAL_NODE_CONNECTION_H_
