// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/vfs.h>
#include <lib/vfs/cpp/flags.h>
#include <lib/vfs/cpp/internal/connection.h>
#include <lib/vfs/cpp/internal/node.h>

namespace vfs {
namespace internal {

Connection::Connection(uint32_t flags) : flags_(flags) {}

Connection::~Connection() = default;

void Connection::Clone(Node* vn, uint32_t flags, zx::channel request,
                       async_dispatcher_t* dispatcher) {
  vn->Clone(flags, flags_, std::move(request), dispatcher);
}

void Connection::Close(Node* vn, fuchsia::io::Node::CloseCallback callback) {
  callback(vn->PreClose(this));
  vn->Close(this);
  // |this| is destroyed at this point.
}

void Connection::Describe(Node* vn, fuchsia::io::Node::DescribeCallback callback) {
  fuchsia::io::NodeInfo info{};
  vn->Describe(&info);
  if (info.has_invalid_tag()) {
    vn->Close(this);
  } else {
    callback(std::move(info));
  }
}

zx_status_t Connection::Bind(zx::channel request, async_dispatcher_t* dispatcher) {
  auto status = BindInternal(std::move(request), dispatcher);
  if (status == ZX_OK && Flags::ShouldDescribe(flags_)) {
    SendOnOpenEvent(status);
  }  // can't send status as binding failed and request object is gone.
  return status;
}

void Connection::Sync(Node* vn, fuchsia::io::Node::SyncCallback callback) {
  // TODO: Check flags.
  callback(vn->Sync());
}

void Connection::GetAttr(Node* vn, fuchsia::io::Node::GetAttrCallback callback) {
  // TODO: Check flags.
  fuchsia::io::NodeAttributes attributes{};
  zx_status_t status = vn->GetAttr(&attributes);
  callback(status, attributes);
}

void Connection::SetAttr(Node* vn, uint32_t flags, fuchsia::io::NodeAttributes attributes,
                         fuchsia::io::Node::SetAttrCallback callback) {
  // TODO: Check flags.
  callback(vn->SetAttr(flags, attributes));
}

std::unique_ptr<fuchsia::io::NodeInfo> Connection::NodeInfoIfStatusOk(Node* vn,
                                                                      zx_status_t status) {
  std::unique_ptr<fuchsia::io::NodeInfo> node_info;
  if (status == ZX_OK) {
    node_info = std::make_unique<fuchsia::io::NodeInfo>();
    vn->Describe(node_info.get());
  }
  return node_info;
}

}  // namespace internal
}  // namespace vfs
