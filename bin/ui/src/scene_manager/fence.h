// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/event.h>

namespace scene_manager {

// Signal values used to signal a mx::event that represents a fence.
// e.g.: fence.signal(0u, kFenceSignalled);
constexpr mx_status_t kFenceSignalled = MX_EVENT_SIGNALED;
constexpr mx_status_t kFenceSignalledOrClosed =
    MX_EVENT_SIGNALED | MX_SIGNAL_LAST_HANDLE;

}  // namespace scene_manager
