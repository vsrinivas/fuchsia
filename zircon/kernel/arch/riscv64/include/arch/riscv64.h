// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_RISCV64_H_
#define ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_RISCV64_H_

#include <arch/regs.h>  // For REGOFF

#define RISCV64_CSR_SMODE_BITS     (1 << 8)

// These CSRs are only in user CSR space (still readable by all modes though)
#define RISCV64_CSR_CYCLE     (0xc00)
#define RISCV64_CSR_TIME      (0xc01)
#define RISCV64_CSR_INSRET    (0xc02)
#define RISCV64_CSR_CYCLEH    (0xc80)
#define RISCV64_CSR_TIMEH     (0xc81)
#define RISCV64_CSR_INSRETH   (0xc82)

#define RISCV64_CSR_SATP      (0x180)

#define RISCV64_CSR_SSTATUS   (0x000 | RISCV64_CSR_SMODE_BITS)
#define RISCV64_CSR_SIE       (0x004 | RISCV64_CSR_SMODE_BITS)
#define RISCV64_CSR_STVEC     (0x005 | RISCV64_CSR_SMODE_BITS)
#define RISCV64_CSR_SSCRATCH  (0x040 | RISCV64_CSR_SMODE_BITS)
#define RISCV64_CSR_SEPC      (0x041 | RISCV64_CSR_SMODE_BITS)
#define RISCV64_CSR_SCAUSE    (0x042 | RISCV64_CSR_SMODE_BITS)
#define RISCV64_CSR_STVAL     (0x043 | RISCV64_CSR_SMODE_BITS)
#define RISCV64_CSR_SIP       (0x044 | RISCV64_CSR_SMODE_BITS)

#define RISCV64_CSR_SSTATUS_IE         (1u << 1)
#define RISCV64_CSR_SSTATUS_PIE        (1u << 5)
#define RISCV64_CSR_SSTATUS_PP         (1u << 8)
#define RISCV64_CSR_SSTATUS_FS         (3u << 13)
#define RISCV64_CSR_SSTATUS_FS_OFF     (0u)
#define RISCV64_CSR_SSTATUS_FS_INITIAL (1u << 13)
#define RISCV64_CSR_SSTATUS_FS_CLEAN   (2u << 13)
#define RISCV64_CSR_SSTATUS_FS_DIRTY   (3u << 13)

#define RISCV64_CSR_SIE_SIE       (1u << 1)
#define RISCV64_CSR_SIE_TIE       (1u << 5)
#define RISCV64_CSR_SIE_EIE       (1u << 9)

#define RISCV64_CSR_SIP_SIP       (1u << 1)
#define RISCV64_CSR_SIP_TIP       (1u << 5)
#define RISCV64_CSR_SIP_EIP       (1u << 9)

// Interrupts, top bit set in cause register
#define RISCV64_INTERRUPT_SSWI        1       // software interrupt
#define RISCV64_INTERRUPT_STIM        5       // timer interrupt
#define RISCV64_INTERRUPT_SEXT        9       // external interrupt

// Exceptions
#define RISCV64_EXCEPTION_IADDR_MISALIGN      0
#define RISCV64_EXCEPTION_IACCESS_FAULT       1
#define RISCV64_EXCEPTION_ILLEGAL_INS         2
#define RISCV64_EXCEPTION_BREAKPOINT          3
#define RISCV64_EXCEPTION_LOAD_ADDR_MISALIGN  4
#define RISCV64_EXCEPTION_LOAD_ACCESS_FAULT   5
#define RISCV64_EXCEPTION_STORE_ADDR_MISALIGN 6
#define RISCV64_EXCEPTION_STORE_ACCESS_FAULT  7
#define RISCV64_EXCEPTION_ENV_CALL_U_MODE     8
#define RISCV64_EXCEPTION_ENV_CALL_S_MODE     9
#define RISCV64_EXCEPTION_ENV_CALL_M_MODE     11
#define RISCV64_EXCEPTION_INS_PAGE_FAULT      12
#define RISCV64_EXCEPTION_LOAD_PAGE_FAULT     13
#define RISCV64_EXCEPTION_STORE_PAGE_FAULT    15

// Byte offsets corresponding to the fields of riscv64_context_switch_frame.
#define CONTEXT_SWITCH_FRAME_OFFSET_RA REGOFF(0)
#define CONTEXT_SWITCH_FRAME_OFFSET_S(n) REGOFF(1 + n)
#define CONTEXT_SWITCH_FRAME_OFFSET_FS(n) REGOFF(13 + n)

#define SIZEOF_CONTEXT_SWITCH_FRAME REGOFF(26)

#ifndef __ASSEMBLER__

#include <assert.h>
#include <stdbool.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <kernel/cpu.h>
#include <arch/defines.h>

__BEGIN_CDECLS

struct iframe_t;

struct arch_exception_context {
    struct iframe_t *frame;
};

struct Thread;

void platform_irq(iframe_t* frame);

__END_CDECLS

#define __ASM_STR(x)    #x

#define riscv64_csr_clear(csr, bits) \
({ \
  ulong __val = bits; \
  __asm__ volatile( \
    "csrc   " __ASM_STR(csr) ", %0" \
    :: "rK" (__val) \
    : "memory"); \
})

#define riscv64_csr_read_clear(csr, bits) \
({ \
  ulong __val = bits; \
  ulong __val_out; \
  __asm__ volatile( \
    "csrrc   %0, " __ASM_STR(csr) ", %1" \
    : "=r"(__val_out) \
    : "rK" (__val) \
    : "memory"); \
  __val_out; \
})

#define riscv64_csr_set(csr, bits) \
({ \
  ulong __val = bits; \
  __asm__ volatile( \
    "csrs   " __ASM_STR(csr) ", %0" \
    :: "rK" (__val) \
    : "memory"); \
})

#define riscv64_csr_read(csr) \
({ \
  ulong __val; \
  __asm__ volatile( \
    "csrr   %0, " __ASM_STR(csr) \
    : "=r" (__val) \
    :: "memory"); \
  __val; \
})

#define riscv64_csr_write(csr, val) \
({ \
  ulong __val = (ulong)val; \
  __asm__ volatile( \
    "csrw   " __ASM_STR(csr) ", %0" \
    :: "rK" (__val) \
    : "memory"); \
  __val; \
})

// Register state layout used by arm64_context_switch().
struct riscv64_context_switch_frame {
  unsigned long ra; // return address (x1)

  unsigned long s0; // x8-x9
  unsigned long s1;

  unsigned long s2; // x18-x27
  unsigned long s3;
  unsigned long s4;
  unsigned long s5;
  unsigned long s6;
  unsigned long s7;
  unsigned long s8;
  unsigned long s9;
  unsigned long s10;
  unsigned long s11;

  unsigned long fs0; // f8-f9
  unsigned long fs1;

  unsigned long fs2; // f18-f27
  unsigned long fs3;
  unsigned long fs4;
  unsigned long fs5;
  unsigned long fs6;
  unsigned long fs7;
  unsigned long fs8;
  unsigned long fs9;
  unsigned long fs10;
  unsigned long fs11;

  unsigned long reserved; // stack alignment
};

static_assert(__offsetof(riscv64_context_switch_frame, ra) == CONTEXT_SWITCH_FRAME_OFFSET_RA, "");
static_assert(__offsetof(riscv64_context_switch_frame, s0) == CONTEXT_SWITCH_FRAME_OFFSET_S(0), "");
static_assert(__offsetof(riscv64_context_switch_frame, fs0) == CONTEXT_SWITCH_FRAME_OFFSET_FS(0), "");
static_assert(sizeof(riscv64_context_switch_frame) == SIZEOF_CONTEXT_SWITCH_FRAME, "");

extern "C" void riscv64_exception_entry(void);
extern "C" void riscv64_context_switch(vaddr_t *old_sp, vaddr_t new_sp);

extern void riscv64_timer_exception();
extern void riscv64_software_exception();

/*
 * Creates a stack and sets the stack pointer for the specified secondary CPU.
 */
zx_status_t riscv64_create_secondary_stack(cpu_num_t cpu_num, vaddr_t *sp);

/*
 * Frees a stack created by |riscv64_create_secondary_stack|.
 */
zx_status_t riscv64_free_secondary_stack(cpu_num_t cpu_num);

#endif  // __ASSEMBLER__

#endif  // ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_RISCV64_H_
