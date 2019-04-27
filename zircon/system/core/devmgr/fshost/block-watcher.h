// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "fs-manager.h"

namespace devmgr {

// Monitors "/dev/class/block" for new devices indefinitely.
void BlockDeviceWatcher(std::unique_ptr<FsManager> fshost, bool netboot, bool check_filesystems);

} // namespace devmgr
