// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syscalls/arm64.h>

.macro zircon_syscall num, name, caller
    mov x16, #\num
    svc #0x0
// This symbol at the return address identifies this as an approved call site.
    .hidden CODE_SYSRET_\name\()_VIA_\caller
CODE_SYSRET_\name\()_VIA_\caller\():
    // Speculation: Certain arm64 processors speculate past an SVC instruction, continuing to
    // execute code after it with the pre-SVC register state. Add a barrier sequence after SVC to
    // prevent further speculative execution after the SVC at EL0.
    //
    // We want to prevent EL0 from speculatively executing instructions that follow the SVC because
    // speculatively executing instructions after the SVC with pre-SVC register state could allow
    // an attacker, either in-process or remote, to leak information from this EL0 process. Because
    // the syscall wrapper and SVC implementation are part of the vDSO provided by Fuchsia, the EL0
    // program would have difficulty protecting itself via other (ex: toolchain) mitigations.
    //
    // https://sourceware.org/bugzilla/attachment.cgi?id=12223 for the bug report/details.
    //
    // The barrier is skipped on return, by advancing $PC by 12. It only needs to constrain
    // speculative, not real executions.
0:  dsb nsh
    isb
    // Add a BRK to the barrier, to 'assert' that its skipped. If this BRK is hit, the 'assert' will
    // trip, visibly, indicating a kernel/vDSO mismatch/error.
    brk 0
1:
.ifne 1b - 0b - ARM64_SYSCALL_SPECULATION_BARRIER_SIZE
.error "code size of speculation barrier is not ARM64_SYSCALL_SPECULATION_BARRIER_SIZE"
.endif
.endm

// CFI aware push and pop macros.
// SP must always be aligned to 16, so never push just one register.
.macro push_regpair reg0, reg1
    stp \reg0, \reg1, [sp, #-16]!
    .cfi_adjust_cfa_offset 16
    .cfi_rel_offset \reg0, 0
    .cfi_rel_offset \reg1, 8
.endm
.macro pop_regpair reg0, reg1
    ldp \reg0, \reg1, [sp], #16
    .cfi_adjust_cfa_offset -16
    .cfi_same_value \reg0
    .cfi_same_value \reg1
.endm
