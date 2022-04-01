// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_PHYS_REGS_H_
#define ZIRCON_KERNEL_ARCH_ARM64_PHYS_REGS_H_

// Offsets into PhysExceptionState for assembly.
#define REGS_X(n) ((n)*8)
#define REGS_LR REGS_X(30)
#define REGS_SP REGS_X(31)
#define REGS_PC REGS_X(32)
#define REGS_CPSR REGS_X(33)
#define REGS_TPIDR REGS_X(34)
#define REGS_ESR REGS_X(35)
#define REGS_FAR REGS_X(36)
#define REGS_XZR REGS_X(37)   // Two words for zx_exception_context_t.
#define REGS_SIZE REGS_X(40)  // Those two plus one to stay 16-byte aligned.

#ifndef __ASSEMBLER__

#include <lib/arch/arm64/system.h>
#include <stddef.h>
#include <stdint.h>

#include <phys/exception.h>
#include <phys/stack.h>

static_assert(offsetof(PhysExceptionState, regs.r) == REGS_X(0));
static_assert(offsetof(PhysExceptionState, regs.lr) == REGS_LR);
static_assert(offsetof(PhysExceptionState, regs.sp) == REGS_SP);
static_assert(offsetof(PhysExceptionState, regs.pc) == REGS_PC);
static_assert(offsetof(PhysExceptionState, regs.cpsr) == REGS_CPSR);
static_assert(offsetof(PhysExceptionState, regs.tpidr) == REGS_TPIDR);
static_assert(offsetof(PhysExceptionState, exc.arch.u.arm_64.esr) == REGS_ESR);
static_assert(offsetof(PhysExceptionState, exc.arch.u.arm_64.far) == REGS_FAR);
static_assert(sizeof(PhysExceptionState) == REGS_SIZE);
static_assert(sizeof(PhysExceptionState) % BOOT_STACK_ALIGN == 0);

// Install the table for the current EL and for all lower ELs.
extern "C" void ArmSetVbar(const void* table);

#endif  // !__ASSEMBLER__

#endif  // ZIRCON_KERNEL_ARCH_ARM64_PHYS_REGS_H_
