// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This header includes FVM-specific functionality that
// may be used by filesystem servers.

#pragma once

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <zircon/device/block.h>

namespace fs {

// Walks through all slices on the partition backed by |fd|, attempting to free
// everything except for the first slice. Does not close |fd|.
zx_status_t fvm_reset_volume_slices(int fd);

}
