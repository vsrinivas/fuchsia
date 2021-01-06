// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/minfs_private.h"

namespace minfs {

bool Minfs::DirtyCacheEnabled() {
#ifdef MINFS_ENABLE_DIRTY_CACHE
  return true;
#else
  return false;
#endif  // MINFS_ENABLE_DIRTY_CACHE
}

}  // namespace minfs
