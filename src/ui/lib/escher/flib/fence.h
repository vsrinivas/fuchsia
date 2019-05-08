// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_FLIB_FENCE_H_
#define SRC_UI_LIB_ESCHER_FLIB_FENCE_H_

#include <lib/zx/event.h>

namespace escher {

// Signal values used to signal a zx::event that represents a fence.
// e.g.: fence.signal(0u, kFenceSignalled);
constexpr zx_status_t kFenceSignalled = ZX_EVENT_SIGNALED;

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_FLIB_FENCE_H_
