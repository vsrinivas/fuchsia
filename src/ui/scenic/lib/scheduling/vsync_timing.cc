// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scheduling/vsync_timing.h"

#include <lib/async/default.h>
#include <lib/async/time.h>

namespace scheduling {

VsyncTiming::VsyncTiming() : last_vsync_time_(0), vsync_interval_(kNsecsFor60fps) {}

}  // namespace scheduling
