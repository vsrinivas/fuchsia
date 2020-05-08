// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>

#include <utility>

#include <fs/remote_file.h>
#include <fs/vfs_types.h>

namespace fio = ::llcpp::fuchsia::io;

namespace fs {

RemoteFile::RemoteFile(zx::channel remote_client) : remote_client_(std::move(remote_client)) {
  ZX_DEBUG_ASSERT(remote_client_);
}

RemoteFile::~RemoteFile() = default;

VnodeProtocolSet RemoteFile::GetProtocols() const { return VnodeProtocol::kFile; }

zx_status_t RemoteFile::GetAttributes(VnodeAttributes* attr) {
  *attr = VnodeAttributes();
  attr->mode = V_TYPE_FILE | V_IRUSR;
  attr->inode = fio::INO_UNKNOWN;
  attr->link_count = 1;
  return ZX_OK;
}

bool RemoteFile::IsRemote() const { return true; }

zx_handle_t RemoteFile::GetRemote() const { return remote_client_.get(); }

zx_status_t RemoteFile::GetNodeInfoForProtocol([[maybe_unused]] VnodeProtocol protocol,
                                               [[maybe_unused]] Rights rights,
                                               VnodeRepresentation* info) {
  *info = VnodeRepresentation::File();
  return ZX_OK;
}

}  // namespace fs
