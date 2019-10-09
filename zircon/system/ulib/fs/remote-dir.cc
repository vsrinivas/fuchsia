// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/remote-dir.h>
#include <fs/vfs_types.h>

#include <utility>

namespace fs {

RemoteDir::RemoteDir(zx::channel remote_dir_client)
    : remote_dir_client_(std::move(remote_dir_client)) {
  ZX_DEBUG_ASSERT(remote_dir_client_);
}

RemoteDir::~RemoteDir() = default;

zx_status_t RemoteDir::GetAttributes(VnodeAttributes* attr) {
  *attr = VnodeAttributes();
  attr->mode = V_TYPE_DIR | V_IRUSR;
  attr->inode = fuchsia_io_INO_UNKNOWN;
  attr->link_count = 1;
  return ZX_OK;
}

bool RemoteDir::IsRemote() const { return true; }

zx_handle_t RemoteDir::GetRemote() const { return remote_dir_client_.get(); }

zx_status_t RemoteDir::GetNodeInfo(Rights rights, fuchsia_io_NodeInfo* info) {
  info->tag = fuchsia_io_NodeInfoTag_directory;
  return ZX_OK;
}

}  // namespace fs
