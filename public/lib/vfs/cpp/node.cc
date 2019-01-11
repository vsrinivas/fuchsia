// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/vfs/cpp/node.h>

#include <algorithm>

#include <lib/vfs/cpp/connection.h>
#include <lib/vfs/cpp/internal/node_connection.h>

namespace vfs {

Node::Node() = default;

Node::~Node() = default;

zx_status_t Node::Close() { return ZX_OK; }

zx_status_t Node::Sync() { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t Node::GetAttr(fuchsia::io::NodeAttributes* out_attributes) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Node::SetAttr(uint32_t flags,
                          const fuchsia::io::NodeAttributes& attributes) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Node::Serve(uint32_t flags, zx::channel request,
                        async_dispatcher_t* dispatcher) {
  auto connection = CreateConnection(flags);
  zx_status_t status = connection->Bind(std::move(request), dispatcher);
  if (status == ZX_OK) {
    connections_.push_back(std::move(connection));
  }
  return status;
}

void Node::RemoveConnection(Connection* connection) {
  std::remove_if(
      connections_.begin(), connections_.end(),
      [connection](const auto& entry) { return entry.get() == connection; });
}

std::unique_ptr<Connection> Node::CreateConnection(uint32_t flags) {
  return std::make_unique<internal::NodeConnection>(flags, this);
}

}  // namespace vfs
