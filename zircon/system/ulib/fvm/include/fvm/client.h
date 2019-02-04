// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This header includes FVM utilities which may be used by clients of
// the volume manager.

#pragma once

#ifndef __Fuchsia__
#error "Fuchsia-only header"
#endif

#include <zircon/device/block.h>

namespace fvm {

// Walks through all slices on the partition backed by |fd|, attempting to free
// everything except for the first slice. Does not close |fd|.
zx_status_t ResetAllSlices(int fd);

}
