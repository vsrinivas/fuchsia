// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_PKGFS_LAUNCHER_H_
#define SRC_STORAGE_FSHOST_PKGFS_LAUNCHER_H_

#include "filesystem-mounter.h"

namespace devmgr {

// Launches pkgfs from within blobfs by parsing environment variables.
void LaunchPkgfs(FilesystemMounter* filesystems);

}  // namespace devmgr

#endif  // SRC_STORAGE_FSHOST_PKGFS_LAUNCHER_H_
