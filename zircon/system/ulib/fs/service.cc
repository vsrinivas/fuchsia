// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/service.h>

#include <fuchsia/io/llcpp/fidl.h>
#include <zircon/device/vfs.h>

#include <utility>

#include <fs/vfs_types.h>

namespace fio = ::llcpp::fuchsia::io;

namespace fs {

Service::Service(Connector connector) : connector_(std::move(connector)) {}

Service::~Service() = default;

VnodeProtocolSet Service::GetProtocols() const { return VnodeProtocol::kConnector; }

zx_status_t Service::GetAttributes(VnodeAttributes* attr) {
  // TODO(fxbug.dev/31095): V_TYPE_FILE isn't right, we should use a type for services
  *attr = VnodeAttributes();
  attr->mode = V_TYPE_FILE;
  attr->inode = fio::INO_UNKNOWN;
  attr->link_count = 1;
  return ZX_OK;
}

zx_status_t Service::ConnectService(zx::channel channel) {
  if (!connector_) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return connector_(std::move(channel));
}

zx_status_t Service::GetNodeInfoForProtocol([[maybe_unused]] VnodeProtocol protocol,
                                            [[maybe_unused]] Rights rights,
                                            VnodeRepresentation* info) {
  *info = VnodeRepresentation::Connector();
  return ZX_OK;
}

}  // namespace fs
