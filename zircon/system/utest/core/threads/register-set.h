// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/syscalls/debug.h>

// This provides some utilities for testing that sets of register values
// are reported correctly.

#if defined(__x86_64__)
#define REG_PC rip
#define REG_STACK_PTR rsp
#elif defined(__aarch64__)
#define REG_PC pc
#define REG_STACK_PTR sp
#else
#error Unsupported architecture
#endif

struct thread_local_regs {
#if defined(__x86_64__)
  uint64_t fs_base_value;
  uint64_t gs_base_value;
#elif defined(__aarch64__)
  uint64_t tpidr_value;
#else
#error Unsupported architecture
#endif
};

// Initializes the register set with known test data.
void general_regs_fill_test_values(zx_thread_state_general_regs_t* regs);
void fp_regs_fill_test_values(zx_thread_state_fp_regs* regs);
void vector_regs_fill_test_values(zx_thread_state_vector_regs* regs);
void debug_regs_fill_test_values(zx_thread_state_debug_regs_t* to_write,
                                 zx_thread_state_debug_regs_t* expected);

// Returns whether the two register sets' values are equal.
bool general_regs_expect_eq(const zx_thread_state_general_regs_t& regs1,
                            const zx_thread_state_general_regs_t& regs2);
bool fp_regs_expect_eq(const zx_thread_state_fp_regs_t& regs1,
                       const zx_thread_state_fp_regs_t& regs2);
bool vector_regs_expect_eq(const zx_thread_state_vector_regs_t& regs1,
                           const zx_thread_state_vector_regs_t& regs2);
bool debug_regs_expect_eq(const char* file, int line, const zx_thread_state_debug_regs_t& regs1,
                          const zx_thread_state_debug_regs_t& regs2);

// The functions below are assembly.
__BEGIN_CDECLS

// These function set the registers to the state specified by |regs|, then branch to
// |spin_address|, a single-instruction infinite loop whose address is |spin_address|.
void spin_with_general_regs(zx_thread_state_general_regs_t* regs);
void spin_with_fp_regs(zx_thread_state_fp_regs_t* regs);
void spin_with_vector_regs(zx_thread_state_vector_regs_t* regs);
void spin_with_debug_regs(zx_thread_state_debug_regs_t* regs);
void spin_address();

// These assembly code routine saves the registers into a corresponding
// structure pointed to by the stack pointer, and then calls zx_thread_exit().
void save_general_regs_and_exit_thread();
void save_fp_regs_and_exit_thread();
void save_vector_regs_and_exit_thread();
void save_thread_local_regs_and_exit_thread();

__END_CDECLS
