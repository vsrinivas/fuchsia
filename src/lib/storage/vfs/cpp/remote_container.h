// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_REMOTE_CONTAINER_H_
#define FS_REMOTE_CONTAINER_H_

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <lib/zx/channel.h>

#include "src/lib/storage/vfs/cpp/vfs.h"

namespace fs {

// RemoteContainer adds support for mounting remote handles on nodes.
class RemoteContainer {
 public:
  constexpr RemoteContainer() = default;
  bool IsRemote() const;
  fidl::ClientEnd<llcpp::fuchsia::io::Directory> DetachRemote();
  fidl::UnownedClientEnd<::llcpp::fuchsia::io::Directory> GetRemote() const;
  void SetRemote(fidl::ClientEnd<llcpp::fuchsia::io::Directory> remote);

 private:
  fidl::ClientEnd<llcpp::fuchsia::io::Directory> remote_;
};

}  // namespace fs

#endif  // FS_REMOTE_CONTAINER_H_
