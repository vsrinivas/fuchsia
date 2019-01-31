// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

// Userspace general regs are stored in two different structs:
// - syscalls = x86_syscall_general_regs_t
// - interrupts/exceptions = x86_iframe_t
// A tagged pointer is stored in struct arch_thread to specify which one.
#define X86_GENERAL_REGS_NONE    0
#define X86_GENERAL_REGS_SYSCALL 1
#define X86_GENERAL_REGS_IFRAME  2

#ifndef __ASSEMBLER__

#include <assert.h>
#include <zircon/compiler.h>
#include <stdint.h>

__BEGIN_CDECLS

// the structure used to hold the general purpose integer registers
// when a syscall is suspended

typedef struct {
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t rsp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rip;
    uint64_t rflags;
} x86_syscall_general_regs_t;

__END_CDECLS

#endif // !__ASSEMBLER__
