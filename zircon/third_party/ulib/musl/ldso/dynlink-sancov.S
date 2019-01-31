// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <asm.h>

// See the end of dynlink.c for what this is about.

.section .text._dynlink_sancov_trace_pc_guard,"ax",%progbits

.weak __sanitizer_cov_trace_pc_guard

ENTRY(_dynlink_sancov_trace_pc_guard)
#ifdef __x86_64__
    jmp *__sanitizer_cov_trace_pc_guard@GOTPCREL(%rip)
#elif defined(__aarch64__)
    adrp x16, :got:__sanitizer_cov_trace_pc_guard
    ldr x16, [x16, #:got_lo12:__sanitizer_cov_trace_pc_guard]
    br x16
#else
# error unsupported architecture
#endif
END(_dynlink_sancov_trace_pc_guard)
.hidden _dynlink_sancov_trace_pc_guard
