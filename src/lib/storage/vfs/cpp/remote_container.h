// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_REMOTE_CONTAINER_H_
#define SRC_LIB_STORAGE_VFS_CPP_REMOTE_CONTAINER_H_

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
  fidl::ClientEnd<fuchsia_io::Directory> DetachRemote();
  fidl::UnownedClientEnd<fuchsia_io::Directory> GetRemote() const;

 private:
  fidl::ClientEnd<fuchsia_io::Directory> remote_;
};

}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_REMOTE_CONTAINER_H_
