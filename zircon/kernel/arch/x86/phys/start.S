// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/asm.h>
#include <lib/arch/ticks.h>
#include <lib/arch/x86/cpuid-asm.h>
#include <lib/arch/x86/msr.h>
#include <zircon/tls.h>

#include "zircon/kernel/phys/stack.h"

// This is the entry point from the boot loader or thereabouts.
// It receives one argument, in %rsi, usually a pointer (physical address).
//
// In a ZBI executable, this is where zbi_kernel_t::entry points and
// %rsi holds the address of the data ZBI.
.function _start, global
  // As early as possible collect the time stamp.
  sample_ticks

  // Clear frame pointer: at the root of the call stack.
  xor %ebp, %ebp

  // Save the timestamp since %rax must be clobbered below.
  mov %rax, %rbx

  // Clear bss.  Note this assumes it's aligned to 8, which is ensured
  // by the bss declaration below.
  lea _edata(%rip), %rdi
  lea _end(%rip), %rcx
  sub %rdi, %rcx
  shr $3, %rcx
  xor %eax, %eax
  cld  // Assume nothing.  ABI requires that DF be clear.
  rep stosq

  // Save first argument to PhysMain in a call-saved register.
  // The time stamp is already safe in %rbx.
  mov %rsi, %r12

  // Get all the cpuid data in place.
  // This only needs the basic stack to call and return.
  lea (boot_stack + BOOT_STACK_SIZE)(%rip), %rsp
  call InitializeBootCpuid

  // Set up the secondary stacks and the thread pointer area.
  lea boot_thread_pointer(%rip), %rax

  // The wrgsbase instruction is more efficient when it's supported.  But using
  // it requires both checking CPUID for its support and then enabling it in
  // %cr4.  There is no real need to bother with any of that in assembly since
  // one wrmsr at boot is not costly enough to bother avoiding.  The kernel can
  // check for and enable fsgsbase instructions easily and cleanly from C++
  // before it does anything that is performance-sensitive enough to warrant
  // preferring them over the MSRs.
  wrmsr64 MSR_IA32_GS_BASE

.Lstack_guard:
  // Use the best hardware randomness available for the stack canary value.
  testl $CPUID_EXTF_RDSEED, (gBootCpuidExtf + CPUID_EBX)(%rip)
  jnz .Lrdseed
  testl $CPUID_FEATURE_RDRAND, (gBootCpuidFeature + CPUID_ECX)(%rip)
  jnz .Lrdrand

  // The only "randomness" readily available is our own load address, so
  // swizzle that in with some arbitrary bits.
  lea _start(%rip), %rcx
  movabs $0xdeadbeef1ee2d00d, %rax
  xor %rcx, %rax
  jmp .Lstack_guard_write

.Lrdrand:
  rdrand %rax
  jmp .Lstack_guard_write

.Lrdseed:
  rdseed %rax

.Lstack_guard_write:
  mov %rax, %gs:ZX_TLS_STACK_GUARD_OFFSET

#if __has_feature(safe_stack)
  lea (boot_unsafe_stack + BOOT_STACK_SIZE)(%rip), %rax
  mov %rax, %gs:ZX_TLS_UNSAFE_SP_OFFSET
#endif

  // Now the full C++ ABI is available.  This could theoretically be a tail
  // call since it's obliged never to return, but it's nice to have the
  // caller in a backtrace (and the call implicitly adjusts the stack
  // alignment as the ABI requires).
  mov %r12, %rdi
  mov %rbx, %rsi
  call PhysMain

  // Trap forever just in case it does return.
0:ud2
  jmp 0b
.end_function

.object boot_thread_area, bss, local, align=8
#if ZX_TLS_UNSAFE_SP_OFFSET < ZX_TLS_STACK_GUARD_OFFSET
  .error "TLS ABI layout??"
#endif
  .skip ZX_TLS_UNSAFE_SP_OFFSET + 8
boot_thread_pointer:
.end_object
