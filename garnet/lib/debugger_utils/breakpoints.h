// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_DEBUGGER_UTILS_BREAKPOINTS_H_
#define GARNET_LIB_DEBUGGER_UTILS_BREAKPOINTS_H_

#include <stdint.h>
#include <zircon/types.h>

namespace debugger_utils {

constexpr const uint8_t kX64BreakpointInstruction[] = { 0xcc };

// ARM64 break instruction (little-endian)
constexpr const uint8_t kArm64BreakpointInstruction[] =
  { 0x00, 0x00, 0x20, 0xd4 };

// For native debugging.
static inline const uint8_t* GetBreakpointInstruction() {
#if defined(__x86_64__)
  return kX64BreakpointInstruction;
#elif defined(__aarch64__)
  return kArm64BreakpointInstruction;
#else
#error "unsupported architecture"
#endif
}

// For native debugging.
static inline uint32_t GetBreakpointInstructionSize() {
#if defined(__x86_64__)
  return sizeof(kX64BreakpointInstruction);
#elif defined(__aarch64__)
  return sizeof(kArm64BreakpointInstruction);
#else
#error "unsupported architecture"
#endif
}

// Given the reported PC after a s/w breakpoint instruction, return the
// address of that instruction.

static inline zx_vaddr_t X64DecrementPcAfterBreak(zx_vaddr_t pc) {
  return pc - sizeof(kX64BreakpointInstruction);
}

static inline zx_vaddr_t Arm64DecrementPcAfterBreak(zx_vaddr_t pc) {
  return pc;
}

// For native debugging.
static inline zx_vaddr_t DecrementPcAfterBreak(zx_vaddr_t pc) {
#if defined(__x86_64__)
  return X64DecrementPcAfterBreak(pc);
#elif defined(__aarch64__)
  return Arm64DecrementPcAfterBreak(pc);
#else
#error "unsupported architecture"
#endif
}

// Given the reported PC after a s/w breakpoint instruction, return the
// address of the next instruction.

static inline zx_vaddr_t X64IncrementPcAfterBreak(zx_vaddr_t pc) {
  return pc;
}

static inline zx_vaddr_t Arm64IncrementPcAfterBreak(zx_vaddr_t pc) {
  return pc + sizeof(kArm64BreakpointInstruction);;
}

// For native debugging.
static inline zx_vaddr_t IncrementPcAfterBreak(zx_vaddr_t pc) {
#if defined(__x86_64__)
  return X64IncrementPcAfterBreak(pc);
#elif defined(__aarch64__)
  return Arm64IncrementPcAfterBreak(pc);
#else
#error "unsupported architecture"
#endif
}

// Trigger a s/w breakpoint instruction.

// For native debugging.
static void inline TriggerSoftwareBreakpoint() {
#if defined(__x86_64__)
  __asm__ volatile("int3");
#elif defined(__aarch64__)
  __asm__ volatile("brk 0");
#else
#error "unsupported architecture"
#endif
}

#ifdef __Fuchsia__

// Resume |thread| after it has executed a s/w breakpoint instruction.

zx_status_t ResumeAfterSoftwareBreakpointInstruction(zx_handle_t thread,
                                                     zx_handle_t eport);

#endif

}  // namespace debugger_utils

#endif  // GARNET_LIB_DEBUGGER_UTILS_BREAKPOINTS_H_
