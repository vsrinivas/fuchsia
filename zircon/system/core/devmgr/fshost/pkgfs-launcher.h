// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "filesystem-mounter.h"

namespace devmgr {

// Launches pkgfs from within blobfs by parsing environment variables.
void LaunchBlobInit(FilesystemMounter* filesystems);

} // namespace devmgr
