// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/clock.h>

#include "src/developer/debug/debug_agent/time.h"

namespace debug_agent {

uint64_t GetNowTimestamp() { return zx::clock::get_monotonic().get(); }

}  // namespace debug_agent
