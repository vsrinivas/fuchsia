// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_UNWIND_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_UNWIND_H_

#include <lib/zx/thread.h>
#include <stdint.h>
#include <zircon/syscalls/debug.h>

#include <vector>

#include "src/developer/debug/ipc/records.h"

namespace debug_agent {

namespace arch {
class ArchProvider;
}

// We're testing different unwinders, this specifies which one you want to use.
// The unwinder type is a process-wide state.
enum class UnwinderType { kNgUnwind, kAndroid };
void SetUnwinderType(UnwinderType unwinder_type);

zx_status_t UnwindStack(arch::ArchProvider* arch_provider, const zx::process& process,
                        uint64_t dl_debug_addr, const zx::thread& thread,
                        const zx_thread_state_general_regs& regs, size_t max_depth,
                        std::vector<debug_ipc::StackFrame>* stack);

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_UNWIND_H_
