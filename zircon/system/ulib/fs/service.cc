// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/service.h>
#include <zircon/device/vfs.h>

#include <utility>

namespace fs {

Service::Service(Connector connector) : connector_(std::move(connector)) {}

Service::~Service() = default;

zx_status_t Service::ValidateOptions([[maybe_unused]] VnodeConnectionOptions options) {
  return ZX_OK;
}

zx_status_t Service::Getattr(vnattr_t* attr) {
  // TODO(ZX-1152): V_TYPE_FILE isn't right, we should use a type for services
  memset(attr, 0, sizeof(vnattr_t));
  attr->mode = V_TYPE_FILE;
  attr->inode = fuchsia_io_INO_UNKNOWN;
  attr->nlink = 1;
  return ZX_OK;
}

zx_status_t Service::Serve(Vfs* vfs, zx::channel channel, fs::VnodeConnectionOptions options) {
  if (!connector_) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return connector_(std::move(channel));
}

bool Service::IsDirectory() const { return false; }

zx_status_t Service::GetNodeInfo([[maybe_unused]] Rights rights, fuchsia_io_NodeInfo* info) {
  info->tag = fuchsia_io_NodeInfoTag_service;
  return ZX_OK;
}

}  // namespace fs
