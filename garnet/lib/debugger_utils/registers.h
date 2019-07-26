// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_DEBUGGER_UTILS_REGISTERS_H_
#define GARNET_LIB_DEBUGGER_UTILS_REGISTERS_H_

#include <stdint.h>
#include <zircon/types.h>
#include <zircon/syscalls/debug.h>

namespace debugger_utils {

zx_status_t ReadGeneralRegisters(zx_handle_t thread, zx_thread_state_general_regs_t* regs);

zx_status_t WriteGeneralRegisters(zx_handle_t thread, const zx_thread_state_general_regs_t* regs);

static inline zx_vaddr_t GetPcFromGeneralRegisters(const zx_thread_state_general_regs_t* regs) {
#if defined(__x86_64__)
  return regs->rip;
#elif defined(__aarch64__)
  return regs->pc;
#else
#error "unsupported architecture"
#endif
}

static inline void SetPcInGeneralRegisters(zx_thread_state_general_regs_t* regs, zx_vaddr_t pc) {
#if defined(__x86_64__)
  regs->rip = pc;
#elif defined(__aarch64__)
  regs->pc = pc;
#else
#error "unsupported architecture"
#endif
}

static inline zx_vaddr_t GetSpFromGeneralRegisters(const zx_thread_state_general_regs_t* regs) {
#if defined(__x86_64__)
  return regs->rsp;
#elif defined(__aarch64__)
  return regs->sp;
#else
#error "unsupported architecture"
#endif
}

static inline void SetSpInGeneralRegisters(zx_thread_state_general_regs_t* regs, zx_vaddr_t sp) {
#if defined(__x86_64__)
  regs->rsp = sp;
#elif defined(__aarch64__)
  regs->sp = sp;
#else
#error "unsupported architecture"
#endif
}

static inline zx_vaddr_t GetFpFromGeneralRegisters(const zx_thread_state_general_regs_t* regs) {
#if defined(__x86_64__)
  return regs->rbp;
#elif defined(__aarch64__)
  return regs->r[29];
#else
#error "unsupported architecture"
#endif
}

static inline void SetFpInGeneralRegisters(zx_thread_state_general_regs_t* regs, zx_vaddr_t fp) {
#if defined(__x86_64__)
  regs->rbp = fp;
#elif defined(__aarch64__)
  regs->r[29] = fp;
#else
#error "unsupported architecture"
#endif
}

}  // namespace debugger_utils

#endif  // GARNET_LIB_DEBUGGER_UTILS_BREAKPOINTS_H_
