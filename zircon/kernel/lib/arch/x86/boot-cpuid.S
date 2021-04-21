// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/asm.h>
#include <lib/arch/x86/cpuid-asm.h>

#ifdef __x86_64__
#define GLOBAL(x) (x)(%rip)
#define LEA_GLOBAL(x, reg) lea (x)(%rip), reg
#define BX %rbx
#define REGSIZE 8
#define LEAVES_START %rsi
#define LEAVES_STOP %rdi
#define DATA_START %r8
#else
#define GLOBAL(x) x
#define LEA_GLOBAL(x, reg) mov $x, reg
#define BX %ebx
#define REGSIZE 4
#define LEAVES_START %esi
#define LEAVES_STOP %edi
#define DATA_START %ebp
#endif // __x86_64__

// Stores CPUID values represented by EAX-EDX at a particular address.
.macro store_cpuid addr
mov %eax, CPUID_EAX + \addr
mov %ebx, CPUID_EBX + \addr
mov %ecx, CPUID_ECX + \addr
mov %edx, CPUID_EDX + \addr
.endm

// This initializes the CpuidIo objects returned by all the arch::BootCpuid
// instantiations linked in.
.function InitializeBootCpuid, global

  push BX
  .cfi_adjust_cfa_offset REGSIZE
  .cfi_rel_offset BX, 0
// We need to make use of another call-saved register in the 32-bit case.
#ifndef __x86_64__
  push DATA_START
  .cfi_adjust_cfa_offset REGSIZE
  .cfi_rel_offset DATA_START, 0
#endif

  // The zeroth leaves (basic, hypervisor, and extended) are special because
  // they tell us what other leaves exist.
  // They are not included in the special section we iterate over below.
  xor %eax,%eax
  xor %ecx,%ecx
  cpuid
  store_cpuid GLOBAL(gBootCpuid0)

  mov $CPUID_HYP_LEAF0, %eax
  xor %ecx,%ecx
  cpuid
  store_cpuid GLOBAL(gBootCpuidHyp0)

  mov $CPUID_EXT_LEAF0, %eax
  xor %ecx,%ecx
  cpuid
  store_cpuid GLOBAL(gBootCpuidExt0)

  // CpuidIo objects are uint32_t[4] data objects (C++ thinks they're a fancier
  // type, but that's the layout).  The arch::BootCpuid instantiations put
  // their objects into the special section named "BootCpuid", so the linker
  // magically defines these two symbols.  The compile-time initialized values
  // give the leaf and subleaf to query.
  LEA_GLOBAL(__start_BootCpuidLeaf, LEAVES_START)
  LEA_GLOBAL(__stop_BootCpuidLeaf, LEAVES_STOP)
  LEA_GLOBAL(__start_BootCpuidData, DATA_START)
  jmp .Loopcheck

.Loop:
  // Load the leaf and check if it's supported. Whether the leaf is supported
  // is dependent on whether it exceeds the maximum supported within the
  // three separate ranges of
  //   [CPUID_EXT_LEAF, UINT32_MAX)
  //   [CPUID_HYP_LEAF, CPUID_EXT_LEAF)
  //   [0, CPUID_HYP_LEAF)
  // Note that we jump to .Lunsupported_leaf if we happen on any of the zeroth
  // leaves, as we do not expect them to be present in the special section and
  // further we expect to have already initiliazed them at the top of the
  // routine.
  mov 0(LEAVES_START), %eax

  // Skip to the hypervisor range if the extended range is unsupported.
  cmpl $0, GLOBAL(gBootCpuidExt0 + CPUID_EAX)
  je .Ltry_hypervisor
  cmp GLOBAL(gBootCpuidExt0 + CPUID_EAX), %eax
  ja .Lunsupported_leaf  // (max extended leaf, UINT32_MAX]
  cmp $CPUID_EXT_LEAF0, %eax
  ja .Linit  // (0th extended leaf, max extended leaf]

.if CPUID_HYP_LEAF0 > CPUID_EXT_LEAF0
.error "unexpected leaf values"
.endif

.Ltry_hypervisor:
  // Skip to the basic range if the hypervisor range is unsupported.
  cmpl $0, GLOBAL(gBootCpuidHyp0 + CPUID_EAX)
  je .Ltry_basic
  cmp GLOBAL(gBootCpuidHyp0 + CPUID_EAX), %eax
  ja .Lunsupported_leaf // (max hypervisor leaf, 0th extended leaf]
  cmp $CPUID_HYP_LEAF0, %eax
  ja .Linit // (0th hypervisor leaf, max hypervisor leaf]

.Ltry_basic:
  cmp GLOBAL(gBootCpuid0 + CPUID_EAX), %eax
  ja .Lunsupported_leaf // (max basic leaf, 0th hypervisor leaf]

.Linit:
  // Load the subleaf and ask the hardware.
  mov 4(LEAVES_START), %ecx
  cpuid

  // Store the register values.
.Lstore:
  store_cpuid 0(DATA_START)

  // Next iteration.
  lea (4 * 4)(DATA_START), DATA_START
  lea (4 * 2)(LEAVES_START), LEAVES_START
.Loopcheck:
  // A link-time assertion ensures that BootCpuidLeaf and BootCpuidData
  // sections have the same number of entries, so we need only track the
  // iteration through BootCpuidLeaf.
  cmp LEAVES_START, LEAVES_STOP
  jne .Loop

  pop BX
  .cfi_adjust_cfa_offset -REGSIZE
  .cfi_same_value BX
#ifndef __x86_64__
  pop DATA_START
  .cfi_adjust_cfa_offset -REGSIZE
  .cfi_same_value DATA_START
#endif

  ret

.Lunsupported_leaf:
  xor %eax, %eax
  xor %ebx, %ebx
  xor %ecx, %ecx
  xor %edx, %edx
  jmp .Lstore

.end_function
