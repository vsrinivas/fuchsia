// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/arch/asm.h>

#if defined(__aarch64__)

// The GOT slot contains the offset from TPIDR_EL0.  Just return it.
//
// Since the function is so tiny, keep it naturally-aligned (it's a mere 8
// bytes) just to be sure it can never straddle a cache line.
.function _ld_tlsdesc_runtime_static, export, align=8
  .cfi.tlsdesc

  // On entry x0 contains the argument: the address of the GOT slot pair.
  // The first word holds our own PC, the second the static TLS offset.
  ldr x0, [x0, #8]

  // On exit x0 contains the return value: offset from $tp (TPIDR_EL0).
  .cfi_undefined x0

  ret
.end_function

#elif defined(__x86_64__)

// The GOT slot contains the offset from %fs.base.  Just return it.
//
// Since the function is so tiny, keep it naturally-aligned (it's actually
// only 5 bytes) just to be sure it can never straddle a cache line.
.function _ld_tlsdesc_runtime_static, export, align=8
  .cfi.tlsdesc

  // On entry %rax contains the argument: the address of the GOT slot pair.
  // The first word holds our own PC, the second the static TLS offset.
  mov 8(%rax), %rax

  // On exit %rax contains the return value: offset from $tp (%fs.base).
  .cfi_undefined %rax

  ret
.end_function

#else

// Not all machines have TLSDESC support specified in the psABI.

#endif
