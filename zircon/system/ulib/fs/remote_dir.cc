// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/remote_dir.h>

#include <fuchsia/io/llcpp/fidl.h>
#include <fs/vfs_types.h>

#include <utility>

namespace fio = ::llcpp::fuchsia::io;

namespace fs {

RemoteDir::RemoteDir(zx::channel remote_dir_client)
    : remote_dir_client_(std::move(remote_dir_client)) {
  ZX_DEBUG_ASSERT(remote_dir_client_);
}

RemoteDir::~RemoteDir() = default;

VnodeProtocolSet RemoteDir::GetProtocols() const { return VnodeProtocol::kDirectory; }

zx_status_t RemoteDir::GetAttributes(VnodeAttributes* attr) {
  *attr = VnodeAttributes();
  attr->mode = V_TYPE_DIR | V_IRUSR;
  attr->inode = fio::INO_UNKNOWN;
  attr->link_count = 1;
  return ZX_OK;
}

bool RemoteDir::IsRemote() const { return true; }

zx_handle_t RemoteDir::GetRemote() const { return remote_dir_client_.get(); }

zx_status_t RemoteDir::GetNodeInfoForProtocol([[maybe_unused]] VnodeProtocol protocol,
                                              [[maybe_unused]] Rights rights,
                                              VnodeRepresentation* info) {
  *info = VnodeRepresentation::Directory();
  return ZX_OK;
}

}  // namespace fs
