// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/remote_container.h"

namespace fs {

bool RemoteContainer::IsRemote() const { return remote_.is_valid(); }

fidl::ClientEnd<llcpp::fuchsia::io::Directory> RemoteContainer::DetachRemote() {
  return std::move(remote_);
}

fidl::UnownedClientEnd<::llcpp::fuchsia::io::Directory> RemoteContainer::GetRemote() const {
  return remote_.borrow();
}

void RemoteContainer::SetRemote(fidl::ClientEnd<llcpp::fuchsia::io::Directory> remote) {
  ZX_DEBUG_ASSERT(!remote_.is_valid());
  remote_ = std::move(remote);
}

}  // namespace fs
