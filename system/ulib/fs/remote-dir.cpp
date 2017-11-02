// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/remote-dir.h>

namespace fs {

RemoteDir::RemoteDir(zx::channel remote_dir_client)
    : remote_dir_client_(fbl::move(remote_dir_client)) {
    ZX_DEBUG_ASSERT(remote_dir_client_);
}

RemoteDir::~RemoteDir() = default;

zx_status_t RemoteDir::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->mode = V_TYPE_DIR | V_IRUSR;
    attr->nlink = 1;
    return ZX_OK;
}

bool RemoteDir::IsRemote() const {
    return true;
}

zx_handle_t RemoteDir::GetRemote() const {
    return remote_dir_client_.get();
}

} // namespace fs
