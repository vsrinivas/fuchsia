// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <unittest/unittest.h>

#include "register-set.h"

void regs_fill_test_values(mx_general_regs_t* regs) {
    for (uint32_t index = 0; index < sizeof(*regs); ++index) {
        ((uint8_t*)regs)[index] = index + 1;
    }
    // Set various flags bits that will read back the same.
#if defined(__x86_64__)
    // Here we set all flag bits that are modifiable from user space or
    // that are not modifiable but are expected to read back as 1, with the
    // exception of the trap flag (bit 8, which would interfere with
    // execution if we set it).
    //
    // Note that setting the direction flag (bit 10) helps test whether the
    // kernel correctly handles taking an interrupt when that flag is set
    // (see MG-998).
    regs->rflags =
        (1 << 0) | // CF: carry flag
        (1 << 1) | // Reserved, always 1
        (1 << 2) | // PF: parity flag
        (1 << 4) | // AF: adjust flag
        (1 << 6) | // ZF: zero flag
        (1 << 7) | // SF: sign flag
        (1 << 9) | // IF: interrupt enable flag (set by kernel)
        (1 << 10) | // DF: direction flag
        (1 << 11) | // OF: overflow flag
        (1 << 14) | // NT: nested task flag
        (1 << 18) | // AC: alignment check flag
        (1 << 21);  // ID: used for testing for CPUID support
#elif defined(__aarch64__)
    // Only set the 4 flag bits that are readable and writable by the
    // instructions "msr nzcv, REG" and "mrs REG, nzcv".
    regs->cpsr = 0xf0000000;
#endif
}

bool regs_expect_eq(mx_general_regs_t* regs1, mx_general_regs_t* regs2) {
    BEGIN_HELPER;
#define CHECK_REG(FIELD) EXPECT_EQ(regs1->FIELD, regs2->FIELD, "Reg " #FIELD)
#if defined(__x86_64__)
    CHECK_REG(rax);
    CHECK_REG(rbx);
    CHECK_REG(rcx);
    CHECK_REG(rdx);
    CHECK_REG(rsi);
    CHECK_REG(rdi);
    CHECK_REG(rbp);
    CHECK_REG(rsp);
    CHECK_REG(r8);
    CHECK_REG(r9);
    CHECK_REG(r10);
    CHECK_REG(r11);
    CHECK_REG(r12);
    CHECK_REG(r13);
    CHECK_REG(r14);
    CHECK_REG(r15);
    CHECK_REG(rip);
    CHECK_REG(rflags);
#elif defined(__aarch64__)
    for (int regnum = 0; regnum < 30; ++regnum) {
        char name[10];
        snprintf(name, sizeof(name), "Reg r[%d]", regnum);
        EXPECT_EQ(regs1->r[regnum], regs2->r[regnum], name);
    }
    CHECK_REG(lr);
    CHECK_REG(sp);
    CHECK_REG(pc);
    CHECK_REG(cpsr);
#else
# error Unsupported architecture
#endif
#undef CHECK_REG
    END_HELPER;
}

// spin_with_regs() function.
#if defined(__x86_64__)
static_assert(offsetof(mx_general_regs_t, rax) == 8*0, "");
static_assert(offsetof(mx_general_regs_t, rbx) == 8*1, "");
static_assert(offsetof(mx_general_regs_t, rcx) == 8*2, "");
static_assert(offsetof(mx_general_regs_t, rdx) == 8*3, "");
static_assert(offsetof(mx_general_regs_t, rsi) == 8*4, "");
static_assert(offsetof(mx_general_regs_t, rdi) == 8*5, "");
static_assert(offsetof(mx_general_regs_t, rbp) == 8*6, "");
static_assert(offsetof(mx_general_regs_t, rsp) == 8*7, "");
static_assert(offsetof(mx_general_regs_t, r8) == 8*8, "");
static_assert(offsetof(mx_general_regs_t, r9) == 8*9, "");
static_assert(offsetof(mx_general_regs_t, r10) == 8*10, "");
static_assert(offsetof(mx_general_regs_t, r11) == 8*11, "");
static_assert(offsetof(mx_general_regs_t, r12) == 8*12, "");
static_assert(offsetof(mx_general_regs_t, r13) == 8*13, "");
static_assert(offsetof(mx_general_regs_t, r14) == 8*14, "");
static_assert(offsetof(mx_general_regs_t, r15) == 8*15, "");
static_assert(offsetof(mx_general_regs_t, rip) == 8*16, "");
static_assert(offsetof(mx_general_regs_t, rflags) == 8*17, "");
static_assert(sizeof(mx_general_regs_t) == 8*18, "");
__asm__(".pushsection .text, \"ax\", @progbits\n"
        ".global spin_with_regs\n"
        "spin_with_regs:\n"
        // Set flags using POPF.  Note that we use POPF rather than SAHF
        // because POPF is able to set more flags than SAHF.
        "pushq 8*17(%rdi)\n"
        "popfq\n"
        // Load general purpose registers.
        "movq 8*0(%rdi), %rax\n"
        "movq 8*1(%rdi), %rbx\n"
        "movq 8*2(%rdi), %rcx\n"
        "movq 8*3(%rdi), %rdx\n"
        "movq 8*4(%rdi), %rsi\n"
        // Skip assigning rdi here and assign it last.
        "movq 8*6(%rdi), %rbp\n"
        "movq 8*7(%rdi), %rsp\n"
        "movq 8*8(%rdi), %r8\n"
        "movq 8*9(%rdi), %r9\n"
        "movq 8*10(%rdi), %r10\n"
        "movq 8*11(%rdi), %r11\n"
        "movq 8*12(%rdi), %r12\n"
        "movq 8*13(%rdi), %r13\n"
        "movq 8*14(%rdi), %r14\n"
        "movq 8*15(%rdi), %r15\n"
        "movq 8*5(%rdi), %rdi\n"
        ".global spin_with_regs_spin_address\n"
        "spin_with_regs_spin_address:\n"
        "jmp spin_with_regs_spin_address\n"
        ".popsection\n");
#elif defined(__aarch64__)
static_assert(offsetof(mx_general_regs_t, r[0]) == 8*0, "");
static_assert(offsetof(mx_general_regs_t, r[1]) == 8*1, "");
static_assert(offsetof(mx_general_regs_t, lr) == 8*30, "");
static_assert(offsetof(mx_general_regs_t, sp) == 8*31, "");
static_assert(offsetof(mx_general_regs_t, pc) == 8*32, "");
static_assert(offsetof(mx_general_regs_t, cpsr) == 8*33, "");
static_assert(sizeof(mx_general_regs_t) == 8*34, "");
__asm__(".pushsection .text, \"ax\", %progbits\n"
        ".global spin_with_regs\n"
        "spin_with_regs:\n"
        // Load sp via a temporary register.
        "ldr x1, [x0, #8*31]\n"
        "mov sp, x1\n"
        // Load NZCV flags, a subset of the PSTATE/CPSR register.
        "ldr x1, [x0, #8*33]\n"
        "msr nzcv, x1\n"
        // Load general purpose registers.
        // Skip assigning x0 and x1 here and assign them last.
        "ldp x2, x3, [x0, #8*2]\n"
        "ldp x4, x5, [x0, #8*4]\n"
        "ldp x6, x7, [x0, #8*6]\n"
        "ldp x8, x9, [x0, #8*8]\n"
        "ldp x10, x11, [x0, #8*10]\n"
        "ldp x12, x13, [x0, #8*12]\n"
        "ldp x14, x15, [x0, #8*14]\n"
        "ldp x16, x17, [x0, #8*16]\n"
        "ldp x18, x19, [x0, #8*18]\n"
        "ldp x20, x21, [x0, #8*20]\n"
        "ldp x22, x23, [x0, #8*22]\n"
        "ldp x24, x25, [x0, #8*24]\n"
        "ldp x26, x27, [x0, #8*26]\n"
        "ldp x28, x29, [x0, #8*28]\n"
        "ldr x30, [x0, #8*30]\n"
        "ldp x0, x1, [x0]\n"
        ".global spin_with_regs_spin_address\n"
        "spin_with_regs_spin_address:\n"
        "b spin_with_regs_spin_address\n"
        ".popsection\n");
#else
# error Unsupported architecture
#endif

// save_regs_and_exit_thread() function.
#if defined(__x86_64__)
__asm__(".pushsection .text,\"ax\", @progbits\n"
        ".global save_regs_and_exit_thread\n"
        "save_regs_and_exit_thread:\n"
        "movq %rax, 8*0(%rsp)\n"
        "movq %rbx, 8*1(%rsp)\n"
        "movq %rcx, 8*2(%rsp)\n"
        "movq %rdx, 8*3(%rsp)\n"
        "movq %rsi, 8*4(%rsp)\n"
        "movq %rdi, 8*5(%rsp)\n"
        "movq %rbp, 8*6(%rsp)\n"
        "movq %rsp, 8*7(%rsp)\n"
        "movq %r8, 8*8(%rsp)\n"
        "movq %r9, 8*9(%rsp)\n"
        "movq %r10, 8*10(%rsp)\n"
        "movq %r11, 8*11(%rsp)\n"
        "movq %r12, 8*12(%rsp)\n"
        "movq %r13, 8*13(%rsp)\n"
        "movq %r14, 8*14(%rsp)\n"
        "movq %r15, 8*15(%rsp)\n"
        // Save the flags register.
        "pushfq\n"
        "popq %rax\n"
        "movq %rax, 8*17(%rsp)\n"
        // Fill out the rip field with known value.
        "leaq save_regs_and_exit_thread(%rip), %rax\n"
        "movq %rax, 8*16(%rsp)\n"
        "call mx_thread_exit@PLT\n"
        "ud2\n"
        ".popsection\n");
#elif defined(__aarch64__)
__asm__(".pushsection .text, \"ax\", %progbits\n"
        ".global save_regs_and_exit_thread\n"
        "save_regs_and_exit_thread:\n"
        "stp x0, x1, [sp, #8*0]\n"
        "stp x2, x3, [sp, #8*2]\n"
        "stp x4, x5, [sp, #8*4]\n"
        "stp x6, x7, [sp, #8*6]\n"
        "stp x8, x9, [sp, #8*8]\n"
        "stp x10, x11, [sp, #8*10]\n"
        "stp x12, x13, [sp, #8*12]\n"
        "stp x14, x15, [sp, #8*14]\n"
        "stp x16, x17, [sp, #8*16]\n"
        "stp x18, x19, [sp, #8*18]\n"
        "stp x20, x21, [sp, #8*20]\n"
        "stp x22, x23, [sp, #8*22]\n"
        "stp x24, x25, [sp, #8*24]\n"
        "stp x26, x27, [sp, #8*26]\n"
        "stp x28, x29, [sp, #8*28]\n"
        "str x30, [sp, #8*30]\n"
        // Save the sp register.
        "mov x0, sp\n"
        "str x0, [sp, #8*31]\n"
        // Fill out the pc field with known value.
        "adr x0, save_regs_and_exit_thread\n"
        "str x0, [sp, #8*32]\n"
        // Save NZCV flags, a subset of the PSTATE/CPSR register.
        "mrs x0, nzcv\n"
        "str x0, [sp, #8*33]\n"
        "bl mx_thread_exit\n"
        "brk 0\n"
        ".popsection\n");
#else
# error Unsupported architecture
#endif
