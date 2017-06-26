// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/vfs/vfs_serve.h"

#include <fcntl.h>

namespace mtl {

bool VFSServe(mxtl::RefPtr<fs::Vnode> directory, mx::channel request) {
  if (directory->Open(O_DIRECTORY) < 0)
    return false;

  mx_handle_t h = request.release();
  if (directory->Serve(h, 0) < 0) {
    directory->Close();
    return false;
  }

  // Setting this signal indicates that this directory is actively being served.
  mx_object_signal_peer(h, 0, MX_USER_SIGNAL_0);
  return true;
}

}  // namespace mtl
