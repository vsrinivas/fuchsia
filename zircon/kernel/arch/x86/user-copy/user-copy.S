// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/asm.h>
#include <zircon/errors.h>

// This file is intended to be compiled several times over with varying
// preprocessor variables defined:
//
// * FUNCTION_NAME - Required: the name of the function.
// * MOVSB - Optional: whether to copy byte-by-byte, instead of copying in
//   larger chunks; the former might be more efficient.
// * SMAP - Optional: whether SMAP is supported.

#ifndef FUNCTION_NAME
#error "FUNCTION_NAME not defined"
#endif

// struct X64CopyToFromUserRet {
//   zx_status_t status;
//   uint pf_flags;
//   vaddr_t pf_va;
// };
//
// X64CopyToFromUserRet FUNCTION_NAME(void* dst, const void *src, size_t len,
//                                    uint64_t* fault_return,
//                                    uint64_t fault_return_mask);
//
// Register use in this code:
// %rdi = argument 1, void* dst
// %rsi = argument 2, const void* src
// %rdx = argument 3, size_t len
//   - moved to %rcx
// %rcx = argument 4, uint64_t* fault_return
//   - moved to %r10
// %r8 = argument 5, uint64_t fault_return_mask
//
.function FUNCTION_NAME, global
  // Copy fault_return out of %rcx, because %rcx is used by `rep movsb` later.
  movq %rcx, %r10
  // Set up page fault return.
  leaq .Lfault_copy(%rip), %rax
  // Given that we are in the kernel and our page fault return address will be a kernel address
  // we can toggle the high bit of fault_return (which controls whether the hardware page fault
  // handler actually runs or not) by simpling ANDing with the supplied fault_return_mask.
  andq %r8, %rax
  movq %rax, (%r10)

#ifdef SMAP
  // Disable SMAP protection, if enabled.
  stac
#endif

  // Between now and the reset of the fault return, we cannot make a function
  // call or manipulate the stack.  We need to be able to restore all callee
  // registers, without any knowledge of where between these two points we
  // faulted.

  // Perform the copy.
#ifdef MOVSB
  // Move one byte at a time.
  movq %rdx, %rcx
  rep movsb
#else
  // Move 8 bytes at a time - and then one byte at a time for the remainder.
  movq %rdx, %rcx
  shrq $3, %rcx
  rep movsq
  andl $7, %edx
  je .Ldone_copy
  movl %edx, %ecx
  rep movsb
#endif

.Ldone_copy:
  // Set eax to ZX_OK. In this case since we do not set rdx the value of the
  // fault address in the return struct is undefined.
  xor %eax, %eax

.Lcleanup_copy:
#ifdef SMAP
  // Re-enable SMAP protection.
  clac
#endif

  // Reset fault return.
  movq $0, (%r10)
  ret
  int3  // AMD SB-1036: Insert int3 after unconditional jmps to constrain speculation

.Lfault_copy:
  // If we are capturing faults the flags will have been placed in rcx and the fault address in
  // rdx. In case we were capturing faults we shuffle the flags to get them into the high bits of
  // rax. It is up to the caller to know if fault capture was enabled and hence whether the flags
  // and fault address will be valid values.
  shlq $32, %rcx
  movl $ZX_ERR_INVALID_ARGS, %eax
  or %rcx, %rax
  jmp .Lcleanup_copy
  int3  // AMD SB-1036: Insert int3 after unconditional jmps to constrain speculation
.end_function
