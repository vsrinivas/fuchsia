// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <string.h>
#include <sys/types.h>
#include <zircon/errors.h>
#include <zircon/syscalls/debug.h>
#include <zircon/types.h>

#include <arch/riscv64.h>
#include <arch/debugger.h>
#include <arch/regs.h>
#include <kernel/thread.h>
#include <kernel/thread_lock.h>

zx_status_t arch_get_general_regs(Thread* thread, zx_thread_state_general_regs_t* out) {
  return ZX_OK;
}

zx_status_t arch_set_general_regs(Thread* thread, const zx_thread_state_general_regs_t* in) {
  return ZX_OK;
}

zx_status_t arch_get_single_step(Thread* thread, zx_thread_state_single_step_t* out) {
  return ZX_OK;
}

zx_status_t arch_set_single_step(Thread* thread, const zx_thread_state_single_step_t* in) {
  return ZX_OK;
}

zx_status_t arch_get_fp_regs(Thread* thread, zx_thread_state_fp_regs* out) {
  return ZX_OK;
}

zx_status_t arch_set_fp_regs(Thread* thread, const zx_thread_state_fp_regs* in) {
  return ZX_OK;
}

zx_status_t arch_get_vector_regs(Thread* thread, zx_thread_state_vector_regs* out) {
  return ZX_OK;
}

zx_status_t arch_set_vector_regs(Thread* thread, const zx_thread_state_vector_regs* in) {
  return ZX_OK;
}

zx_status_t arch_get_debug_regs(Thread* thread, zx_thread_state_debug_regs* out) {
  return ZX_OK;
}

zx_status_t arch_set_debug_regs(Thread* thread, const zx_thread_state_debug_regs* in) {
  return ZX_OK;
}

zx_status_t arch_get_x86_register_fs(Thread* thread, uint64_t* out) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t arch_set_x86_register_fs(Thread* thread, const uint64_t* in) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t arch_get_x86_register_gs(Thread* thread, uint64_t* out) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t arch_set_x86_register_gs(Thread* thread, const uint64_t* in) {
  return ZX_ERR_NOT_SUPPORTED;
}

uint8_t arch_get_hw_breakpoint_count() {
  return ZX_OK;
}

uint8_t arch_get_hw_watchpoint_count() {
  return ZX_OK;
}
