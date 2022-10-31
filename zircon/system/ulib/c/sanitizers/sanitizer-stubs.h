// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_C_SANITIZERS_SANITIZER_STUBS_H_
#define ZIRCON_SYSTEM_ULIB_C_SANITIZERS_SANITIZER_STUBS_H_

// SANITIZER_STUB_ASM is a helper macro used by HWASAN/SANCOV_STUBS for creating
// local trampolines that work around PLT calls. This is mainly used by libc in
// the startup code path before PLT calls can be made. The compiler can emit PLT
// calls when sanitizers instrument calls into their runtimes. Making a weak
// reference to a local symbol will allow the linker to elide the PLT entry and
// resolve calls to this definition.
//
// Example usage:
//
//   ```
//   #include "sanitizer-stubs.h"
//   #include "sancov-stubs.h"
//   #define SANCOV_STUB(name) SANCOV_STUB_ASM("__sanitizer_cov_" #name)
//   #define SANCOV_STUB_ASM(name)  \
//     SANITIZER_STUB_ASM(name, SANITIZER_STUB_ASM_BODY(name))
//   SANCOV_STUBS
//   ```
//
//   This will define stubs that look like:
//
//          .hidden _dynlink__sanitizer_cov_trace_pc_guard
//          .section        .text._dynlink_trampoline__sanitizer_cov_trace_pc_guard,"ax",@progbits
//          .local  _dynlink_trampoline__sanitizer_cov_trace_pc_guard
//          .type   _dynlink_trampoline__sanitizer_cov_trace_pc_guard,@function
//  _dynlink_trampoline__sanitizer_cov_trace_pc_guard:
//          adrp    x16, _dynlink_runtime
//          ldr     w16, [x16, :lo12:_dynlink_runtime]
//          cbnz    w16, _dynlink__sanitizer_cov_trace_pc_guard
//          ret
//  .Ltmp1:
//          .size   _dynlink_trampoline__sanitizer_cov_trace_pc_guard,
//          .Ltmp1-_dynlink_trampoline__sanitizer_cov_trace_pc_guard .text

#define SANITIZER_STUB_ASM(name, trampoline_body)                                           \
  __asm__(".weakref " name ", _dynlink_trampoline" name                                     \
          "\n"                                                                              \
          ".hidden _dynlink" name                                                           \
          "\n"                                                                              \
          ".pushsection .text._dynlink_trampoline" name                                     \
          ",\"ax\",%progbits\n"                                                             \
          ".local _dynlink_trampoline" name                                                 \
          "\n"                                                                              \
          ".type _dynlink_trampoline" name                                                  \
          ",%function\n"                                                                    \
          "_dynlink_trampoline" name ":\n" trampoline_body ".size _dynlink_trampoline" name \
          ", . - _dynlink_trampoline" name                                                  \
          "\n"                                                                              \
          ".popsection");

// SANITIZER_STUB_ASM_BODY is a helper macro that can be used as the
// `trampoline_body` in SANITIZER_STUB_ASM. It will dispatch to a _dynlink*
// stub depending on the value of a local `_dynlink_runtime` flag.

#ifdef __x86_64__
#define SANITIZER_STUB_ASM_BODY(name) \
  "cmpl $0, _dynlink_runtime(%rip)\n" \
  "jne _dynlink" name                 \
  "\n"                                \
  "ret\n"
#elif defined(__aarch64__)
#if __has_feature(hwaddress_sanitizer)
// With hwasan instrumentation on globals, _dynlink_runtime can be tagged so we
// can't get the address directly since its value can be outside the range of
// the corresponding relocation. This effectively does the same thing but
// without an overflow check and manually adds the tag back in.
#define SANITIZER_STUB_ASM_BODY(name)                  \
  "adrp x16, :pg_hi21_nc:_dynlink_runtime\n"           \
  "movk x16, #:prel_g3:_dynlink_runtime+0x100000000\n" \
  "ldr  w16, [x16, #:lo12:_dynlink_runtime]\n"         \
  "cbnz w16, _dynlink" name                            \
  "\n"                                                 \
  "ret\n"
#else
#define SANITIZER_STUB_ASM_BODY(name)         \
  "adrp x16, _dynlink_runtime\n"              \
  "ldr w16, [x16, #:lo12:_dynlink_runtime]\n" \
  "cbnz w16, _dynlink" name                   \
  "\n"                                        \
  "ret\n"
#endif
#else
#error unsupported architecture
#endif

#ifdef __ASSEMBLER__
.macro sanitizer_stub name
  .pushsection .text._dynlink\name,"ax",%progbits
  .weak \name
  ENTRY(_dynlink\name)
#ifdef __x86_64__
    jmp *\name@GOTPCREL(%rip)
#elif defined(__aarch64__)
    adrp x16, :got:\name
    ldr x16, [x16, #:got_lo12:\name]
    br x16
#else
#error unsupported architecture
#endif
  END(_dynlink\name)
  .hidden _dynlink\name
  .popsection
.endm
#endif  // __ASM__

#endif  // ZIRCON_SYSTEM_ULIB_C_SANITIZERS_SANITIZER_STUBS_H_
