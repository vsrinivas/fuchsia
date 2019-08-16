// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_SYSTEM_INFO_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_SYSTEM_INFO_H_

#include <lib/zx/process.h>
#include <zircon/types.h>

#include <vector>

#include "src/developer/debug/ipc/records.h"

namespace debug_agent {

// Fills the root with the process tree of the current system.
zx_status_t GetProcessTree(debug_ipc::ProcessTreeRecord* root);

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_SYSTEM_INFO_H_
