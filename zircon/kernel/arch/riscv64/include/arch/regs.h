// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_RISCV64_IFRAME_H_
#define ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_RISCV64_IFRAME_H_

#ifndef __ASSEMBLER__

#include <assert.h>
#include <stdint.h>
#include <zircon/compiler.h>

// Registers saved on entering the kernel via architectural exception.
struct iframe_t {
  unsigned long scratch;
  unsigned long sp;
  unsigned long epc;
  unsigned long status;
  unsigned long ra;
  unsigned long tp;
  unsigned long a0;
  unsigned long a1;
  unsigned long a2;
  unsigned long a3;
  unsigned long a4;
  unsigned long a5;
  unsigned long a6;
  unsigned long a7;
  unsigned long t0;
  unsigned long t1;
  unsigned long t2;
  unsigned long t3;
  unsigned long t4;
  unsigned long t5;
  unsigned long t6;
};

// Registers saved on entering the kernel via syscall.
using syscall_regs_t = iframe_t;

#endif  // !__ASSEMBLER__

#endif  // ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_RISCV64_IFRAME_H_
