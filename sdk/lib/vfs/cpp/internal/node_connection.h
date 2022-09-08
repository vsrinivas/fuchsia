// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_VFS_CPP_INTERNAL_NODE_CONNECTION_H_
#define LIB_VFS_CPP_INTERNAL_NODE_CONNECTION_H_

#include <fuchsia/io/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/vfs/cpp/internal/connection.h>

#include <memory>

namespace vfs {

namespace internal {
class Node;

// Binds an implementation of |fuchsia.io.Node| to a |vfs::internal::Node|.
class NodeConnection final : public Connection, public fuchsia::io::Node {
 public:
  // Create a connection to |vn| with the given |flags|.
  NodeConnection(fuchsia::io::OpenFlags flags, vfs::internal::Node* vn);
  ~NodeConnection() override;

  // Start listening for |fuchsia.io.Node| messages on |request|.
  zx_status_t BindInternal(zx::channel request, async_dispatcher_t* dispatcher) override;

  // |fuchsia::io::Node| Implementation:
  void Clone(fuchsia::io::OpenFlags flags,
             fidl::InterfaceRequest<fuchsia::io::Node> object) override;
  void Close(CloseCallback callback) override;
  void Query(QueryCallback callback) override;
  void Describe(DescribeCallback callback) override;
  void GetConnectionInfo(GetConnectionInfoCallback callback) override;
  void Sync(SyncCallback callback) override;
  void GetAttr(GetAttrCallback callback) override;
  void SetAttr(fuchsia::io::NodeAttributeFlags flags, fuchsia::io::NodeAttributes attributes,
               SetAttrCallback callback) override;
  void GetFlags(GetFlagsCallback callback) override;
  void SetFlags(fuchsia::io::OpenFlags flags, SetFlagsCallback callback) override;
  void QueryFilesystem(QueryFilesystemCallback callback) override;

 protected:
  // |Connection| Implementation:
  void SendOnOpenEvent(zx_status_t status) override;

 private:
  vfs::internal::Node* vn_;
  fidl::Binding<fuchsia::io::Node> binding_;
};

}  // namespace internal
}  // namespace vfs

#endif  // LIB_VFS_CPP_INTERNAL_NODE_CONNECTION_H_
