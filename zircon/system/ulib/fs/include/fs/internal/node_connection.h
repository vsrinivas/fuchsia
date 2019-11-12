// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_INTERNAL_NODE_CONNECTION_H_
#define FS_INTERNAL_NODE_CONNECTION_H_

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

class NodeConnection final : public Connection {
 public:
  // Refer to documentation for |Connection::Connection|.
  NodeConnection(fs::Vfs* vfs, fbl::RefPtr<fs::Vnode> vnode, zx::channel channel,
                 VnodeProtocol protocol, VnodeConnectionOptions options)
      : Connection(vfs, std::move(vnode), std::move(channel), protocol, options) {}

  ~NodeConnection() final = default;

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

  constexpr static fuchsia_io_Node_ops_t kOps = ([] {
    using Binder = fidl::Binder<NodeConnection>;
    return fuchsia_io_Node_ops_t{
        .Clone = Binder::BindMember<&NodeConnection::Clone>,
        .Close = Binder::BindMember<&NodeConnection::Close>,
        .Describe = Binder::BindMember<&NodeConnection::Describe>,
        .Sync = Binder::BindMember<&NodeConnection::Sync>,
        .GetAttr = Binder::BindMember<&NodeConnection::GetAttr>,
        .SetAttr = Binder::BindMember<&NodeConnection::SetAttr>,
        .NodeGetFlags = Binder::BindMember<&NodeConnection::NodeGetFlags>,
        .NodeSetFlags = Binder::BindMember<&NodeConnection::NodeSetFlags>,
    };
  })();
};

}  // namespace internal

}  // namespace fs

#endif  // FS_INTERNAL_NODE_CONNECTION_H_
