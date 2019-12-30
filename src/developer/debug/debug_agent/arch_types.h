// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_TYPES_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_TYPES_H_

// This file contains some types that are specific to the current CPU architecture.
//
// Anything that does not require a per-CPU type should go through the abtract ArchProvider to
// allow mocking.

namespace debug_agent {
namespace arch {

#if defined(__x86_64__)

// The type that is large enough to hold the debug breakpoint CPU instruction.
using BreakInstructionType = uint8_t;

// Extractors for special registers.
inline uint64_t* IPInRegs(zx_thread_state_general_regs* regs) { return &regs->rip; }
inline uint64_t* SPInRegs(zx_thread_state_general_regs* regs) { return &regs->rsp; }

#elif defined(__aarch64__)

using BreakInstructionType = uint32_t;

// Extractors for special registers.
inline uint64_t* IPInRegs(zx_thread_state_general_regs* regs) { return &regs->pc; }
inline uint64_t* SPInRegs(zx_thread_state_general_regs* regs) { return &regs->sp; }

#else
#error
#endif

}  // namespace arch
}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_TYPES_H_
