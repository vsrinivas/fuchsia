// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>

// void arm64_uspace_entry(uintptr_t arg1, uintptr_t arg2, uintptr_t pc, uintptr_t sp, vaddr_t kstack, uint32_t spsr) __NO_RETURN;
FUNCTION(arm64_uspace_entry)
    mov sp, x4
    msr sp_el0, x3
    msr elr_el1, x2
    msr spsr_el1, x5

    mov x2, xzr
    mov x3, xzr
    mov x4, xzr
    mov x5, xzr
    mov x6, xzr
    mov x7, xzr
    mov x8, xzr
    mov x9, xzr
    mov x10, xzr
    mov x11, xzr
    mov x12, xzr
    mov x13, xzr
    mov x14, xzr
    mov x15, xzr
    mov x16, xzr
    mov x17, xzr
    mov x18, xzr
    mov x19, xzr
    mov x20, xzr
    mov x21, xzr
    mov x22, xzr
    mov x23, xzr
    mov x24, xzr
    mov x25, xzr
    mov x26, xzr
    mov x27, xzr
    mov x28, xzr
    mov x29, xzr
    mov x30, xzr

    // Lazy loading of the FPU means we don't need to zero the simd registers
    eret
END_FUNCTION(arm64_uspace_entry)
