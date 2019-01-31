// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <asm.h>

// These could be in C if we compiled the one file without
// -fsanitize-coverage.  But they are trivial enough in assembly.

.macro trap
#ifdef __x86_64__
    ud2
#elif defined(__aarch64__)
    brk #1000
#else
# error unsupported architecture
#endif
.endm

// This should never be called, because the runtime should have been
// loaded before any module initializers get called.
ENTRY(__sanitizer_cov_trace_pc_guard_init)
    trap
END(__sanitizer_cov_trace_pc_guard_init)
.weak __sanitizer_cov_trace_pc_guard_init

// This is called only from __asan_early_init, which is the only thing
// called during dynamic linker startup before the runtime has been
// loaded that's outside dynlink.c, where _dynlink_sancov_trampoline
// short-circuits before calling here.  Just sanity-check that we
// aren't getting here after module initializers have run.
ENTRY(__sanitizer_cov_trace_pc_guard)
#ifdef __x86_64__
    cmpl $0, (%rdi)
    jne 0f
    ret
#elif defined(__aarch64__)
    ldr w16, [x0]
    cbnz w16, 0f
    ret
#else
# error unsupported architecture
#endif
0:  trap
END(__sanitizer_cov_trace_pc_guard)
.weak __sanitizer_cov_trace_pc_guard
