// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/remote_container.h>

namespace fs {

bool RemoteContainer::IsRemote() const { return remote_.is_valid(); }

zx::channel RemoteContainer::DetachRemote() { return std::move(remote_); }

zx_handle_t RemoteContainer::GetRemote() const { return remote_.get(); }

void RemoteContainer::SetRemote(zx::channel remote) {
  ZX_DEBUG_ASSERT(!remote_.is_valid());
  remote_ = std::move(remote);
}

}  // namespace fs
