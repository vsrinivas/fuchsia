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
#define START %rsi
#define STOP %rdi
#else
#define GLOBAL(x) x
#define LEA_GLOBAL(x, reg) mov $x, reg
#define START %esi
#define STOP %edi
#endif // __x86_64__

// This initializes the CpuidIo objects returned by all the arch::BootCpuid
// instantiations linked in.
.function InitializeBootCpuid, global

  // Leaf 0 is special because it tells us what other leaves exist.
  // This is not included in the special section we iterate over below.
  xor %eax, %eax
  cpuid
  mov %eax, GLOBAL(gBootCpuid0 + CPUID_EAX)
  mov %ebx, GLOBAL(gBootCpuid0 + CPUID_EBX)
  mov %ecx, GLOBAL(gBootCpuid0 + CPUID_ECX)
  mov %edx, GLOBAL(gBootCpuid0 + CPUID_EDX)

  // CpuidIo objects are uint32_t[4] data objects (C++ thinks they're a fancier
  // type, but that's the layout).  The arch::BootCpuid instantiations put
  // their objects into the special section named "BootCpuid", so the linker
  // magically defines these two symbols.  The compile-time initialized values
  // give the leaf and subleaf to query.
  LEA_GLOBAL(__start_BootCpuid, START)
  LEA_GLOBAL(__stop_BootCpuid, STOP)
  jmp .Loopcheck

.Loop:
  // Load the leaf and check if it's supported.
  mov CPUID_EAX(START), %eax
  cmp %eax, GLOBAL(gBootCpuid0 + CPUID_EAX)
  jb .Lunsupported_leaf

  // Load the subleaf and ask the hardware.
  mov CPUID_ECX(START), %ecx
  cpuid

  // Store the register values.
.Lstore:
  mov %eax, CPUID_EAX(START)
  mov %ebx, CPUID_EBX(START)
  mov %ecx, CPUID_ECX(START)
  mov %edx, CPUID_EDX(START)

  // Next iteration.
  lea (4 * 4)(START), START
.Loopcheck:
  cmp START, STOP
  jne .Loop

  ret

.Lunsupported_leaf:
  xor %eax, %eax
  xor %ebx, %ebx
  xor %ecx, %ecx
  xor %edx, %edx
  jmp .Lstore

.end_function
