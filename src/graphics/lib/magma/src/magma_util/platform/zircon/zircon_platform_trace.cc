// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include "platform_trace.h"

namespace magma {

// static
uint64_t PlatformTrace::GetCurrentTicks() { return zx_ticks_get(); }

}  // namespace magma
