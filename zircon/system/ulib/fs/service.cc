// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/service.h>
#include <zircon/device/vfs.h>

#include <utility>

#include <fs/vfs_types.h>

namespace fs {

Service::Service(Connector connector) : connector_(std::move(connector)) {}

Service::~Service() = default;

zx_status_t Service::ValidateOptions(VnodeConnectionOptions options) {
  if (options.flags.directory) {
    return ZX_ERR_NOT_DIR;
  }
  return ZX_OK;
}

zx_status_t Service::GetAttributes(VnodeAttributes* attr) {
  // TODO(ZX-1152): V_TYPE_FILE isn't right, we should use a type for services
  *attr = VnodeAttributes();
  attr->mode = V_TYPE_FILE;
  attr->inode = fuchsia_io_INO_UNKNOWN;
  attr->link_count = 1;
  return ZX_OK;
}

zx_status_t Service::Serve(Vfs* vfs, zx::channel channel, fs::VnodeConnectionOptions options) {
  if (!connector_) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return connector_(std::move(channel));
}

bool Service::IsDirectory() const { return false; }

zx_status_t Service::GetNodeInfo([[maybe_unused]] Rights rights, VnodeRepresentation* info) {
  *info = fs::VnodeRepresentation::Connector();
  return ZX_OK;
}

}  // namespace fs
