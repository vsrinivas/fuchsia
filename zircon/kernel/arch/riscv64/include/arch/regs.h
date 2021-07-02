// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_RISCV64_IFRAME_H_
#define ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_RISCV64_IFRAME_H_

// Returns the offset in bytes of register field numbered x in iframe_t.
#define REGOFF(x) ((x) * 8)

// Byte offsets corresponding to the fields of iframe_t.
#define IFRAME_T_OFFSET_SCRATCH REGOFF(0)
#define IFRAME_T_OFFSET_SP REGOFF(1)
#define IFRAME_T_OFFSET_EPC REGOFF(2)
#define IFRAME_T_OFFSET_STATUS REGOFF(3)
#define IFRAME_T_OFFSET_RA REGOFF(4)
#define IFRAME_T_OFFSET_TP REGOFF(5)
#define IFRAME_T_OFFSET_A(n) REGOFF(6 + n)
#define IFRAME_T_OFFSET_T(n) REGOFF(14 + n)
#define IFRAME_T_OFFSET_FCSR REGOFF(21)
#define IFRAME_T_OFFSET_FA(n) REGOFF(22 + n)
#define IFRAME_T_OFFSET_FT(n) REGOFF(30 + n)

#define SIZEOF_IFRAME_T REGOFF(42)

#ifndef __ASSEMBLER__

#include <assert.h>
#include <stdint.h>
#include <zircon/compiler.h>

// Registers saved on entering the kernel via architectural exception.
// Each field in this structure has a corresponding #define offset below.
// They must be kept in sync.
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

  // Floating point state.
  unsigned long fcsr;
  unsigned long fa0;
  unsigned long fa1;
  unsigned long fa2;
  unsigned long fa3;
  unsigned long fa4;
  unsigned long fa5;
  unsigned long fa6;
  unsigned long fa7;
  unsigned long ft0;
  unsigned long ft1;
  unsigned long ft2;
  unsigned long ft3;
  unsigned long ft4;
  unsigned long ft5;
  unsigned long ft6;
  unsigned long ft7;
  unsigned long ft8;
  unsigned long ft9;
  unsigned long ft10;
  unsigned long ft11;
};

// Registers saved on entering the kernel via syscall.
using syscall_regs_t = iframe_t;

static_assert(__offsetof(iframe_t, scratch) == IFRAME_T_OFFSET_SCRATCH, "");
static_assert(__offsetof(iframe_t, sp) == IFRAME_T_OFFSET_SP, "");
static_assert(__offsetof(iframe_t, epc) == IFRAME_T_OFFSET_EPC, "");
static_assert(__offsetof(iframe_t, status) == IFRAME_T_OFFSET_STATUS, "");
static_assert(__offsetof(iframe_t, ra) == IFRAME_T_OFFSET_RA, "");
static_assert(__offsetof(iframe_t, tp) == IFRAME_T_OFFSET_TP, "");
static_assert(__offsetof(iframe_t, a0) == IFRAME_T_OFFSET_A(0), "");
static_assert(__offsetof(iframe_t, t0) == IFRAME_T_OFFSET_T(0), "");
static_assert(__offsetof(iframe_t, fcsr) == IFRAME_T_OFFSET_FCSR, "");
static_assert(__offsetof(iframe_t, fa0) == IFRAME_T_OFFSET_FA(0), "");
static_assert(__offsetof(iframe_t, ft0) == IFRAME_T_OFFSET_FT(0), "");
static_assert(sizeof(iframe_t) == SIZEOF_IFRAME_T, "");

#endif  // !__ASSEMBLER__

#endif  // ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_RISCV64_IFRAME_H_
