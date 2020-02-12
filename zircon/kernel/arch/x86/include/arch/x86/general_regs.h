// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_GENERAL_REGS_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_GENERAL_REGS_H_

#ifndef __ASSEMBLER__

#include <assert.h>
#include <stdint.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// the structure used to hold the general purpose integer registers
// when a syscall is suspended

typedef struct {
  uint64_t rax;
  uint64_t rbx;
  uint64_t rcx;
  uint64_t rdx;
  uint64_t rsi;
  uint64_t rdi;
  uint64_t rbp;
  uint64_t rsp;
  uint64_t r8;
  uint64_t r9;
  uint64_t r10;
  uint64_t r11;
  uint64_t r12;
  uint64_t r13;
  uint64_t r14;
  uint64_t r15;
  uint64_t rip;
  uint64_t rflags;
} x86_syscall_general_regs_t;

__END_CDECLS

#endif  // !__ASSEMBLER__

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_GENERAL_REGS_H_
