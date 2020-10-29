// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_MSR_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_MSR_H_

// This provides access to x86 Model-Specific Registers (MSRs).
// It defines the constants for the MSR identifiers and it defines
// hwreg types to represent the bit layouts.

#ifdef __ASSEMBLER__  // clang-format off

#ifdef __x86_64__
// Writes %rax to the given MSR, which should be the bare constant.
// Clobbers %rcx and %rdx.
.macro wrmsr64 msr
  mov $\msr, %ecx
  mov %rax, %rdx
  shr $32, %rdx
  wrmsr
.endm
#endif  // __x86_64__

#if defined(__x86_64__) || defined(__i386__)
// Writes %eax to the given MSR, which should be the bare constant.
// Clobbers %ecx and %edx.
.macro wrmsr32 msr
  mov $\msr, %ecx
  xor %edx, %edx
  wrmsr
.endm
#endif  // __x86_64__ || __i386__

// This generated header provides `#define MSR_NAME ...` constants for
// the X86Msr::NAME values below.
#include <lib/arch/x86/msr-asm.h>

#else  // clang-format on

#include <stdint.h>

// MSR identifiers.  These use the ALL_CAPS name style to be consistent with
// the Intel manuals.  The generated header <lib/arch/x86/msr-asm.h> contains
// macros for `MSR_<name>` so these constants can be used in assembly code.
enum class X86Msr : uint32_t {
  IA32_FS_BASE = 0xc000'0100,         // Current %fs.base value.
  IA32_GS_BASE = 0xc000'0101,         // Current %gs.base value.
  IA32_KERNEL_GS_BASE = 0xc000'0102,  // %gs.base value after `swapgs`.

  IA32_DEBUGCTL = 0x0000'01d9,  // Debug control.

  IA32_PERF_CAPABILITIES = 0x0000'0345,  // Performance monitoring features available.

  // Related to Last Branch Records.
  MSR_LBR_SELECT = 0x0000'01c8,            // Control register for the LBR feature
  MSR_LASTBRANCH_TOS = 0x0000'01c9,        // Current top of stack of LBRs.
  MSR_LASTBRANCH_0_FROM_IP = 0x0000'0680,  // Source information of 0th LBR.
  MSR_LASTBRANCH_0_TO_IP = 0x0000'06c0,    // Destination information of 0th LBR.
  MSR_LBR_INFO_0 = 0x0000'0dc0,            // Additional information of 0th LBR.
};

// TODO: add more MSRs, add hwreg types for C++ access

#endif  // __ASSEMBLER__

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_MSR_H_
