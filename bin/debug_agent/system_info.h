// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/process.h>
#include <zircon/types.h>
#include <vector>

#include "garnet/lib/debug_ipc/records.h"

namespace debug_agent {

// Fills the root with the process tree of the current system.
zx_status_t GetProcessTree(debug_ipc::ProcessTreeRecord* root);

// Returns a process handle for the given process koid. The process will be
// not is_valid() on failure.
zx::process GetProcessFromKoid(zx_koid_t koid);

}  // namespace debug_agent
