// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/vfs.h>
#include <lib/fidl/cpp/event_sender.h>
#include <lib/vfs/cpp/flags.h>
#include <lib/vfs/cpp/service.h>

namespace vfs {

Service::Service(Connector connector) : connector_(std::move(connector)) {}

Service::~Service() = default;

void Service::Describe(fuchsia::io::NodeInfoDeprecated* out_info) {
  out_info->set_service(fuchsia::io::Service());
}

void Service::GetConnectionInfo(fuchsia::io::ConnectionInfo* out_info) { *out_info = {}; }

zx_status_t Service::CreateConnection(fuchsia::io::OpenFlags flags,
                                      std::unique_ptr<vfs::internal::Connection>* connection) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Service::Connect(fuchsia::io::OpenFlags flags, zx::channel request,
                             async_dispatcher_t* dispatcher) {
  if (Flags::IsNodeReference(flags)) {
    // Node::Connect will send an OnOpen event if OPEN_FLAG_DESCRIBE is set.
    return Node::Connect(flags, std::move(request), dispatcher);
  }
  // Send OnOpen event if required before switching from the |Node| protocol back to the service.
  if (Flags::ShouldDescribe(flags)) {
    fidl::EventSender<fuchsia::io::Node> sender(std::move(request));
    std::unique_ptr<fuchsia::io::NodeInfoDeprecated> node_info =
        std::make_unique<fuchsia::io::NodeInfoDeprecated>();
    Describe(node_info.get());
    sender.events().OnOpen(ZX_OK, std::move(node_info));
    request = sender.TakeChannel();
  }
  if (connector_ == nullptr) {
    SendOnOpenEventOnError(flags, std::move(request), ZX_ERR_NOT_SUPPORTED);
    return ZX_ERR_NOT_SUPPORTED;
  }
  connector_(std::move(request), dispatcher);
  return ZX_OK;
}

zx_status_t Service::GetAttr(fuchsia::io::NodeAttributes* out_attributes) const {
  out_attributes->mode = fuchsia::io::MODE_TYPE_SERVICE | V_IRUSR;
  out_attributes->id = fuchsia::io::INO_UNKNOWN;
  out_attributes->content_size = 0;
  out_attributes->storage_size = 0;
  out_attributes->link_count = 1;
  out_attributes->creation_time = 0;
  out_attributes->modification_time = 0;
  return ZX_OK;
}

fuchsia::io::OpenFlags Service::GetAllowedFlags() const {
  return fuchsia::io::OpenFlags::RIGHT_READABLE | fuchsia::io::OpenFlags::RIGHT_WRITABLE;
}

fuchsia::io::OpenFlags Service::GetProhibitiveFlags() const { return {}; }

}  // namespace vfs
