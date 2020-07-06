// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_PROCESS_INFO_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_PROCESS_INFO_H_

#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <zircon/syscalls/debug.h>
#include <zircon/types.h>

#include <vector>

#include "src/developer/debug/ipc/records.h"

struct zx_info_process;

namespace debug_agent {

// Fills the given vector with the module information for the process.
// "dl_debug_addr" is the address inside "process" of the dynamic loader's
// debug state.
//
// TODO(brettw) move to ProcessHandle when the unwinder uses it.
zx_status_t GetModulesForProcess(const zx::process& process, uint64_t dl_debug_addr,
                                 std::vector<debug_ipc::Module>* modules);

debug_ipc::ThreadRecord::State ThreadStateToEnums(
    uint32_t state, debug_ipc::ThreadRecord::BlockedReason* blocked_reason);

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_PROCESS_INFO_H_
