// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <asm.h>

#include "sancov-stubs.h"

// See the end of dynlink.c for what this is about.

.macro sancov_stub name
    .pushsection .text._dynlink_sancov_\name,"ax",%progbits
    .weak __sanitizer_cov_\name
    ENTRY(_dynlink_sancov_\name)
#ifdef __x86_64__
        jmp *__sanitizer_cov_\name@GOTPCREL(%rip)
#elif defined(__aarch64__)
        adrp x16, :got:__sanitizer_cov_\name
        ldr x16, [x16, #:got_lo12:__sanitizer_cov_\name]
        br x16
#elif defined(__riscv)
#else
# error unsupported architecture
#endif
    END(_dynlink_sancov_\name)
    .hidden _dynlink_sancov_\name
    .popsection
.endm

#define SANCOV_STUB(name) sancov_stub name;
SANCOV_STUBS
