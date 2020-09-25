// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "register-set.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <zircon/hw/debug/arm64.h>

#include <zxtest/zxtest.h>

namespace {

// Write a NaN double value to the given uint64_t (which is how most of the
// registers are stored in the structs).
void WriteNaNDouble(uint64_t* output) {
  double nan_value = nan("");
  memcpy(output, &nan_value, sizeof(double));
}

}  // namespace

// Fill Test Values -------------------------------------------------------------------------------

void general_regs_fill_test_values(zx_thread_state_general_regs_t* regs) {
  for (uint32_t index = 0; index < sizeof(*regs); ++index) {
    ((uint8_t*)regs)[index] = static_cast<uint8_t>(index + 1);
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
  // (see fxbug.dev/30944).
  regs->rflags = (1 << 0) |   // CF: carry flag
                 (1 << 1) |   // Reserved, always 1
                 (1 << 2) |   // PF: parity flag
                 (1 << 4) |   // AF: adjust flag
                 (1 << 6) |   // ZF: zero flag
                 (1 << 7) |   // SF: sign flag
                 (1 << 9) |   // IF: interrupt enable flag (set by kernel)
                 (1 << 10) |  // DF: direction flag
                 (1 << 11) |  // OF: overflow flag
                 (1 << 14) |  // NT: nested task flag
                 (1 << 18) |  // AC: alignment check flag
                 (1 << 21);   // ID: used for testing for CPUID support

  // Set these to canonical addresses to avoid an error.
  regs->fs_base = 0x0;
  regs->gs_base = 0x0;
  regs->rip = 0x0;
#elif defined(__aarch64__)
  // Only set the 4 flag bits that are readable and writable by the
  // instructions "msr nzcv, REG" and "mrs REG, nzcv".
  regs->cpsr = 0xf0000000;
  regs->tpidr = 0;
#endif
}

void fp_regs_fill_test_values(zx_thread_state_fp_regs* regs) {
  memset(regs, 0, sizeof(zx_thread_state_fp_regs));
#if defined(__x86_64__)
  for (size_t i = 0; i < 7; i++)
    regs->st[i].low = i;

  // Write NaN to the last value.
  WriteNaNDouble(&regs->st[7].low);
#elif defined(__aarch64__)
// No FP struct on ARM (vector only).
#else
#error Unsupported architecture
#endif
}

void vector_regs_fill_test_values(zx_thread_state_vector_regs* regs) {
  memset(regs, 0, sizeof(zx_thread_state_vector_regs));
#if defined(__x86_64__)
  for (uint64_t i = 0; i < 16; i++) {
    // Only sets the XMM registers (first two) since that's all that's guaranteed.
    regs->zmm[i].v[0] = i;
    regs->zmm[i].v[1] = i << 8;
    regs->zmm[i].v[2] = 0;
    regs->zmm[i].v[3] = 0;
  }

  // Write NaN to the last value.
  WriteNaNDouble(&regs->zmm[15].v[0]);
#elif defined(__aarch64__)
  for (uint64_t i = 0; i < 32; i++) {
    regs->v[i].low = i;
    regs->v[i].high = i << 8;
  }

  // Write NaN to the last value.
  WriteNaNDouble(&regs->v[31].low);
#else
#error Unsupported architecture
#endif
}

void debug_regs_fill_test_values(zx_thread_state_debug_regs_t* to_write,
                                 zx_thread_state_debug_regs_t* expected) {
  uint64_t base = reinterpret_cast<uint64_t>(debug_regs_fill_test_values);
#if defined(__x86_64__)
  // The kernel will validate that the addresses set into the debug registers are valid userspace
  // one. We use values relative to this function, as it is guaranteed to be in the userspace
  // range.
  to_write->dr[0] = base;
  to_write->dr[1] = base + 0x4000;
  to_write->dr[2] = base + 0x8000;
  to_write->dr[3] = 0x0;  // Zero is also valid.
  to_write->dr6 = 0;
  to_write->dr7 = 0x33;  // Activate all breakpoints.

  expected->dr[0] = base;
  expected->dr[1] = base + 0x4000;
  expected->dr[2] = base + 0x8000;
  expected->dr[3] = 0x0;
  expected->dr6 = 0xffff0ff0;  // No breakpoint event detected.
  expected->dr7 = 0x733;       // Activate all breakpoints.

#elif defined(__aarch64__)
  *to_write = {};

  // We only set two because we know that arm64 ensures that.
  ARM64_DBGBCR_E_SET(&to_write->hw_bps[0].dbgbcr, 1);
  ARM64_DBGBCR_E_SET(&to_write->hw_bps[1].dbgbcr, 1);
  to_write->hw_bps[0].dbgbvr = base;
  to_write->hw_bps[1].dbgbvr = base + 0x4000;

  ARM64_DBGWCR_E_SET(&to_write->hw_wps[0].dbgwcr, 1);
  ARM64_DBGWCR_BAS_SET(&to_write->hw_wps[0].dbgwcr, 0xf);
  ARM64_DBGWCR_LSC_SET(&to_write->hw_wps[0].dbgwcr, 0b11);
  ARM64_DBGWCR_E_SET(&to_write->hw_wps[1].dbgwcr, 1);
  ARM64_DBGWCR_BAS_SET(&to_write->hw_wps[1].dbgwcr, 0xf0);
  to_write->hw_wps[0].dbgwvr = base;
  to_write->hw_wps[1].dbgwvr = base + 0x4000;

  *expected = *to_write;
  ARM64_DBGBCR_PMC_SET(&expected->hw_bps[0].dbgbcr, 0b10);
  ARM64_DBGBCR_BAS_SET(&expected->hw_bps[0].dbgbcr, 0xf);
  ARM64_DBGBCR_PMC_SET(&expected->hw_bps[1].dbgbcr, 0b10);
  ARM64_DBGBCR_BAS_SET(&expected->hw_bps[1].dbgbcr, 0xf);

  ARM64_DBGWCR_PAC_SET(&expected->hw_wps[0].dbgwcr, 0b10);
  ARM64_DBGWCR_LSC_SET(&expected->hw_wps[0].dbgwcr, 0b11);
  ARM64_DBGWCR_SSC_SET(&expected->hw_wps[0].dbgwcr, 1);
  ARM64_DBGWCR_PAC_SET(&expected->hw_wps[1].dbgwcr, 0b10);
  ARM64_DBGWCR_LSC_SET(&expected->hw_wps[1].dbgwcr, 0);
  ARM64_DBGWCR_SSC_SET(&expected->hw_wps[1].dbgwcr, 1);
#else
#error Unsupported architecture
#endif
}

// Expect Eq Functions ----------------------------------------------------------------------------

void general_regs_expect_eq(const zx_thread_state_general_regs_t& regs1,
                            const zx_thread_state_general_regs_t& regs2) {
#define CHECK_REG(FIELD) EXPECT_EQ(regs1.FIELD, regs2.FIELD, "Reg " #FIELD)
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
    EXPECT_EQ(regs1.r[regnum], regs2.r[regnum], "Reg r[%d]", regnum);
  }
  CHECK_REG(lr);
  CHECK_REG(sp);
  CHECK_REG(pc);
  CHECK_REG(cpsr);
#else
#error Unsupported architecture
#endif
#undef CHECK_REG
}

void fp_regs_expect_eq(const zx_thread_state_fp_regs_t& regs1,
                       const zx_thread_state_fp_regs_t& regs2) {
#if defined(__x86_64__)
  // This just tests the MMX registers.
  EXPECT_EQ(regs1.st[0].low, regs2.st[0].low, "Reg st[0].low");
  EXPECT_EQ(regs1.st[1].low, regs2.st[1].low, "Reg st[1].low");
  EXPECT_EQ(regs1.st[2].low, regs2.st[2].low, "Reg st[2].low");
  EXPECT_EQ(regs1.st[3].low, regs2.st[3].low, "Reg st[3].low");
  EXPECT_EQ(regs1.st[4].low, regs2.st[4].low, "Reg st[4].low");
  EXPECT_EQ(regs1.st[5].low, regs2.st[5].low, "Reg st[5].low");
  EXPECT_EQ(regs1.st[6].low, regs2.st[6].low, "Reg st[6].low");
  EXPECT_EQ(regs1.st[7].low, regs2.st[7].low, "Reg st[7].low");
#elif defined(__aarch64__)
  // No FP regs on ARM (uses vector regs for FP).
  (void)regs1;
  (void)regs2;
#else
#error Unsupported architecture
#endif
}

void vector_regs_expect_unsupported_are_zero(const zx_thread_state_vector_regs_t& regs) {
#if defined(__x86_64__)
  // For the first 16 ZMM registers, we currently support only the lowest 256-bits.  All others
  // should be 0.
  for (int reg = 0; reg < 16; reg++) {
    for (int i = 4; i < 8; i++) {
      EXPECT_EQ(regs.zmm[reg].v[i], 0);
    }
  }
  // The next 16 ZMM registers are unsupported.
  for (int reg = 16; reg < 32; reg++) {
    for (int i = 0; i < 8; i++) {
      EXPECT_EQ(regs.zmm[reg].v[i], 0);
    }
  }
#elif defined(__aarch64__)
  // All features/fields are supported on arm64.
#else
#error Unsupported architecture
#endif
}

void vector_regs_expect_eq(const zx_thread_state_vector_regs_t& regs1,
                           const zx_thread_state_vector_regs_t& regs2) {
#if defined(__x86_64__)
  // Only check the first 16 registers (guaranteed to work).
  for (int reg = 0; reg < 16; reg++) {
    // Only check the low 128 bits (guaranteed to work).
    EXPECT_EQ(regs1.zmm[reg].v[0], regs2.zmm[reg].v[0]);
    EXPECT_EQ(regs1.zmm[reg].v[1], regs2.zmm[reg].v[1]);
  }
#elif defined(__aarch64__)
  for (int i = 0; i < 32; i++) {
    EXPECT_EQ(regs1.v[i].high, regs2.v[i].high);
    EXPECT_EQ(regs1.v[i].low, regs2.v[i].low);
  }
#else
#error Unsupported architecture
#endif
}

void debug_regs_expect_eq(const char* file, int line, const zx_thread_state_debug_regs_t& regs1,
                          const zx_thread_state_debug_regs_t& regs2) {
#if defined(__x86_64__)
  EXPECT_EQ(regs1.dr[0], regs2.dr[0], "%s:%d: %s", file, line, "Reg DR0");
  EXPECT_EQ(regs1.dr[1], regs2.dr[1], "%s:%d: %s", file, line, "Reg DR1");
  EXPECT_EQ(regs1.dr[2], regs2.dr[2], "%s:%d: %s", file, line, "Reg DR2");
  EXPECT_EQ(regs1.dr[3], regs2.dr[3], "%s:%d: %s", file, line, "Reg DR3");
  EXPECT_EQ(regs1.dr6, regs2.dr6, "%s:%d: %s", file, line, "Reg DR6");
  EXPECT_EQ(regs1.dr7, regs2.dr7, "%s:%d: %s", file, line, "Reg DR7");
#elif defined(__aarch64__)
  for (uint32_t i = 0; i < 16; i++) {
    EXPECT_EQ(regs1.hw_bps[i].dbgbcr, regs2.hw_bps[i].dbgbcr);
    EXPECT_EQ(regs1.hw_bps[i].dbgbvr, regs2.hw_bps[i].dbgbvr);
  }

  for (uint32_t i = 0; i < 16; i++) {
    EXPECT_EQ(regs1.hw_wps[i].dbgwcr, regs2.hw_wps[i].dbgwcr);
    EXPECT_EQ(regs1.hw_wps[i].dbgwvr, regs2.hw_wps[i].dbgwvr);
  }

  EXPECT_EQ(regs1.esr, regs2.esr);
  EXPECT_EQ(regs1.far, regs2.far);
#else
#error Unsupported architecture
#endif
}

// Spin Functions --------------------------------------------------------------------------------
#if defined(__x86_64__)
__asm__(
    ".global spin_address\n"
    "spin_address:\n"
    "jmp spin_address\n");
#elif defined(__aarch64__)
__asm__(
    ".global spin_address\n"
    "spin_address:\n"
    "b spin_address\n");
#endif

// spin_with_general_regs() function.
#if defined(__x86_64__)
static_assert(offsetof(zx_thread_state_general_regs_t, rax) == 8 * 0, "");
static_assert(offsetof(zx_thread_state_general_regs_t, rbx) == 8 * 1, "");
static_assert(offsetof(zx_thread_state_general_regs_t, rcx) == 8 * 2, "");
static_assert(offsetof(zx_thread_state_general_regs_t, rdx) == 8 * 3, "");
static_assert(offsetof(zx_thread_state_general_regs_t, rsi) == 8 * 4, "");
static_assert(offsetof(zx_thread_state_general_regs_t, rdi) == 8 * 5, "");
static_assert(offsetof(zx_thread_state_general_regs_t, rbp) == 8 * 6, "");
static_assert(offsetof(zx_thread_state_general_regs_t, rsp) == 8 * 7, "");
static_assert(offsetof(zx_thread_state_general_regs_t, r8) == 8 * 8, "");
static_assert(offsetof(zx_thread_state_general_regs_t, r9) == 8 * 9, "");
static_assert(offsetof(zx_thread_state_general_regs_t, r10) == 8 * 10, "");
static_assert(offsetof(zx_thread_state_general_regs_t, r11) == 8 * 11, "");
static_assert(offsetof(zx_thread_state_general_regs_t, r12) == 8 * 12, "");
static_assert(offsetof(zx_thread_state_general_regs_t, r13) == 8 * 13, "");
static_assert(offsetof(zx_thread_state_general_regs_t, r14) == 8 * 14, "");
static_assert(offsetof(zx_thread_state_general_regs_t, r15) == 8 * 15, "");
static_assert(offsetof(zx_thread_state_general_regs_t, rip) == 8 * 16, "");
static_assert(offsetof(zx_thread_state_general_regs_t, rflags) == 8 * 17, "");
static_assert(offsetof(zx_thread_state_general_regs_t, fs_base) == 8 * 18, "");
static_assert(offsetof(zx_thread_state_general_regs_t, gs_base) == 8 * 19, "");
static_assert(sizeof(zx_thread_state_general_regs_t) == 8 * 20, "");
__asm__(
    ".pushsection .text, \"ax\", @progbits\n"
    ".global spin_with_general_regs\n"
    "spin_with_general_regs:\n"
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

    ".global spin_address\n"
    "jmp spin_address\n"
    ".popsection\n");
#elif defined(__aarch64__)
static_assert(offsetof(zx_thread_state_general_regs_t, r[0]) == 8 * 0, "");
static_assert(offsetof(zx_thread_state_general_regs_t, r[1]) == 8 * 1, "");
static_assert(offsetof(zx_thread_state_general_regs_t, lr) == 8 * 30, "");
static_assert(offsetof(zx_thread_state_general_regs_t, sp) == 8 * 31, "");
static_assert(offsetof(zx_thread_state_general_regs_t, pc) == 8 * 32, "");
static_assert(offsetof(zx_thread_state_general_regs_t, cpsr) == 8 * 33, "");
static_assert(sizeof(zx_thread_state_general_regs_t) == 8 * 35, "");
__asm__(
    ".pushsection .text, \"ax\", %progbits\n"
    ".global spin_with_general_regs\n"
    "spin_with_general_regs:\n"
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

    ".global spin_address\n"
    "b spin_address\n"
    ".popsection\n");
#else
#error Unsupported architecture
#endif

// spin_with_fp_regs() function.
#if defined(__x86_64__)
static_assert(offsetof(zx_thread_state_fp_regs_t, fcw) == 0, "");
static_assert(offsetof(zx_thread_state_fp_regs_t, fsw) == 2, "");
static_assert(offsetof(zx_thread_state_fp_regs_t, ftw) == 4, "");
static_assert(offsetof(zx_thread_state_fp_regs_t, fop) == 6, "");
static_assert(offsetof(zx_thread_state_fp_regs_t, fip) == 8, "");
static_assert(offsetof(zx_thread_state_fp_regs_t, fdp) == 16, "");
static_assert(offsetof(zx_thread_state_fp_regs_t, st) == 32, "");
__asm__(
    ".pushsection .text, \"ax\", @progbits\n"
    ".global spin_with_fp_regs\n"
    "spin_with_fp_regs:\n"

    // rdi = &zx_thread_state_fp_regs_t.st[0]
    "lea 32(%rdi), %rdi\n"

    "movq $0x9999, %rax\n"
    "movq %rax, %xmm0\n"

    "movq 16*0(%rdi), %mm0\n"
    "movq 16*1(%rdi), %mm1\n"
    "movq 16*2(%rdi), %mm2\n"
    "movq 16*3(%rdi), %mm3\n"
    "movq 16*4(%rdi), %mm4\n"
    "movq 16*5(%rdi), %mm5\n"
    "movq 16*6(%rdi), %mm6\n"
    "movq 16*7(%rdi), %mm7\n"

    ".global spin_address\n"
    "jmp spin_address\n"
    ".popsection\n");
#elif defined(__aarch64__)
// Just spins and does nothing. ARM64 doesn't define a separate FP state, but doing this allows the
// rest of the code to be platform-independent.
__asm__(
    ".pushsection .text, \"ax\", %progbits\n"
    ".global spin_with_fp_regs\n"
    "spin_with_fp_regs:\n"

    // Do nothing.

    ".global spin_address\n"
    "b spin_address\n"
    ".popsection\n");
#else
#error Unsupported architecture
#endif

// spin_with_vector_regs() function.
#if defined(__x86_64__)
__asm__(
    ".pushsection .text, \"ax\", @progbits\n"
    ".global spin_with_vector_regs\n"
    "spin_with_vector_regs:\n"

    // rdi = zmm[0] on call. This only loads xmm registers which are guaranteed to exist.
    // Each zmm input is 512 bits = 64 bytes.
    "movdqu 64*0(%rdi), %xmm0\n"
    "movdqu 64*1(%rdi), %xmm1\n"
    "movdqu 64*2(%rdi), %xmm2\n"
    "movdqu 64*3(%rdi), %xmm3\n"
    "movdqu 64*4(%rdi), %xmm4\n"
    "movdqu 64*5(%rdi), %xmm5\n"
    "movdqu 64*6(%rdi), %xmm6\n"
    "movdqu 64*7(%rdi), %xmm7\n"
    "movdqu 64*8(%rdi), %xmm8\n"
    "movdqu 64*9(%rdi), %xmm9\n"
    "movdqu 64*10(%rdi), %xmm10\n"
    "movdqu 64*11(%rdi), %xmm11\n"
    "movdqu 64*12(%rdi), %xmm12\n"
    "movdqu 64*13(%rdi), %xmm13\n"
    "movdqu 64*14(%rdi), %xmm14\n"
    "movdqu 64*15(%rdi), %xmm15\n"

    ".global spin_address\n"
    "jmp spin_address\n"
    ".popsection\n");
#elif defined(__aarch64__)
static_assert(offsetof(zx_thread_state_vector_regs_t, fpcr) == 0, "");
static_assert(offsetof(zx_thread_state_vector_regs_t, fpsr) == 4, "");
static_assert(offsetof(zx_thread_state_vector_regs_t, v) == 8, "");
__asm__(
    ".pushsection .text, \"ax\", %progbits\n"
    ".global spin_with_vector_regs\n"
    "spin_with_vector_regs:\n"

    // FPCR and FPSR are first.
    "ldp w1, w2, [x0]\n"
    "msr fpcr, x1\n"
    "msr fpsr, x2\n"

    // Skip to the vector registers.
    "add x0, x0, 8\n"

    // Each register is 128 bits = 16 bytes, so each pair is 32 bytes.
    "ldp q0, q1, [x0, #(0 * 32)]\n"
    "ldp q2, q3, [x0, #(1 * 32)]\n"
    "ldp q4, q5, [x0, #(2 * 32)]\n"
    "ldp q6, q7, [x0, #(3 * 32)]\n"
    "ldp q8, q9, [x0, #(4 * 32)]\n"
    "ldp q10, q11, [x0, #(5 * 32)]\n"
    "ldp q12, q13, [x0, #(6 * 32)]\n"
    "ldp q14, q15, [x0, #(7 * 32)]\n"
    "ldp q16, q17, [x0, #(8 * 32)]\n"
    "ldp q18, q19, [x0, #(9 * 32)]\n"
    "ldp q20, q21, [x0, #(10 * 32)]\n"
    "ldp q22, q23, [x0, #(11 * 32)]\n"
    "ldp q24, q25, [x0, #(12 * 32)]\n"
    "ldp q26, q27, [x0, #(13 * 32)]\n"
    "ldp q28, q29, [x0, #(14 * 32)]\n"
    "ldp q30, q31, [x0, #(15 * 32)]\n"

    ".global spin_address\n"
    "b spin_address\n"
    ".popsection\n");
#else
#error Unsupported architecture
#endif

// spin_with_debug_regs() function.

// spin_with_debug_regs
#if defined(__x86_64__)
static_assert(offsetof(zx_thread_state_debug_regs_t, dr) == 8 * 0, "");
static_assert(offsetof(zx_thread_state_debug_regs_t, dr6) == 8 * 4, "");
static_assert(offsetof(zx_thread_state_debug_regs_t, dr7) == 8 * 5, "");
__asm__(
    ".pushsection .text, \"ax\", @progbits\n"
    ".global spin_with_debug_regs\n"
    "spin_with_debug_regs:\n"

    // Do nothing.
    // The register state will be set through syscalls because setting the debug registers
    // is a privileged instruction.

    ".global spin_address\n"
    "jmp spin_address\n"
    ".popsection\n");
#elif defined(__aarch64__)
__asm__(
    ".pushsection .text, \"ax\", %progbits\n"
    ".global spin_with_debug_regs\n"
    "spin_with_debug_regs:\n"

    // Do nothing.
    // The register state will be set through syscalls because setting the debug registers
    // is a privileged instruction.

    ".global spin_address\n"
    "b spin_address\n"
    ".popsection\n");
#else
#error Unsupported architecture
#endif

// Save and Exit Functions ------------------------------------------------------------------------

// save_general_regs_and_exit_thread() function.
#if defined(__x86_64__)
__asm__(
    ".pushsection .text,\"ax\", @progbits\n"
    ".global save_general_regs_and_exit_thread\n"
    "save_general_regs_and_exit_thread:\n"
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
    "leaq save_general_regs_and_exit_thread(%rip), %rax\n"
    "movq %rax, 8*16(%rsp)\n"
    "jmp zx_thread_exit@PLT\n"
    "ud2\n"
    ".popsection\n");
#elif defined(__aarch64__)
__asm__(
    ".pushsection .text, \"ax\", %progbits\n"
    ".global save_general_regs_and_exit_thread\n"
    "save_general_regs_and_exit_thread:\n"
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
    "adr x0, save_general_regs_and_exit_thread\n"
    "str x0, [sp, #8*32]\n"
    // Save NZCV flags, a subset of the PSTATE/CPSR register.
    "mrs x0, nzcv\n"
    "str x0, [sp, #8*33]\n"
    "bl zx_thread_exit\n"
    "brk 0\n"
    ".popsection\n");
#else
#error Unsupported architecture
#endif

// save_fp_regs_and_exit_thread() function.
#if defined(__x86_64__)
static_assert(offsetof(zx_thread_state_fp_regs, st) == 32, "");
__asm__(
    ".pushsection .text,\"ax\", @progbits\n"
    ".global save_fp_regs_and_exit_thread\n"
    "save_fp_regs_and_exit_thread:\n"

    // This only saves the low 64 bits, which is the MMX register. Each slot in the struct is
    // 128 bits so need to add 16 bytes each time. The 32 bytes is the start of the FP regs in
    // the struct (see static assert above).
    "movq %mm0, 32 + 16*0(%rsp)\n"
    "movq %mm1, 32 + 16*1(%rsp)\n"
    "movq %mm2, 32 + 16*2(%rsp)\n"
    "movq %mm3, 32 + 16*3(%rsp)\n"
    "movq %mm4, 32 + 16*4(%rsp)\n"
    "movq %mm5, 32 + 16*5(%rsp)\n"
    "movq %mm6, 32 + 16*6(%rsp)\n"
    "movq %mm7, 32 + 16*7(%rsp)\n"

    "jmp zx_thread_exit@PLT\n"
    "ud2\n"
    ".popsection\n");
#elif defined(__aarch64__)
__asm__(
    ".pushsection .text, \"ax\", %progbits\n"
    ".global save_fp_regs_and_exit_thread\n"
    "save_fp_regs_and_exit_thread:\n"

    // Does nothing (no FP values).

    "bl zx_thread_exit\n"
    "brk 0\n"
    ".popsection\n");
#else
#error Unsupported architecture
#endif

// save_vector_regs_and_exit_thread() function.
#if defined(__x86_64__)
static_assert(offsetof(zx_thread_state_vector_regs, zmm) == 0, "");
__asm__(
    ".pushsection .text,\"ax\", @progbits\n"
    ".global save_vector_regs_and_exit_thread\n"
    "save_vector_regs_and_exit_thread:\n"

    // Each vector is 512 bits (64 bytes). We only read the first 128 (xmm registers).
    "movdqu %xmm0, 64*0(%rsp)\n"
    "movdqu %xmm1, 64*1(%rsp)\n"
    "movdqu %xmm2, 64*2(%rsp)\n"
    "movdqu %xmm3, 64*3(%rsp)\n"
    "movdqu %xmm4, 64*4(%rsp)\n"
    "movdqu %xmm5, 64*5(%rsp)\n"
    "movdqu %xmm6, 64*6(%rsp)\n"
    "movdqu %xmm7, 64*7(%rsp)\n"
    "movdqu %xmm8, 64*8(%rsp)\n"
    "movdqu %xmm9, 64*9(%rsp)\n"
    "movdqu %xmm10, 64*10(%rsp)\n"
    "movdqu %xmm11, 64*11(%rsp)\n"
    "movdqu %xmm12, 64*12(%rsp)\n"
    "movdqu %xmm13, 64*13(%rsp)\n"
    "movdqu %xmm14, 64*14(%rsp)\n"
    "movdqu %xmm15, 64*15(%rsp)\n"

    "jmp zx_thread_exit@PLT\n"
    "ud2\n"
    ".popsection\n");
#elif defined(__aarch64__)
__asm__(
    ".pushsection .text, \"ax\", %progbits\n"
    ".global save_vector_regs_and_exit_thread\n"
    "save_vector_regs_and_exit_thread:\n"

    // Input is in SP.
    "mov x0, sp\n"

    // FPCR and FPSR.
    "mrs x1, fpcr\n"
    "mrs x2, fpsr\n"
    "stp w1, w2, [x0]\n"

    // Skip to the vector registers
    "add x0, x0, 8\n"

    // Each register is 128 bits = 16 bytes, so each pair is 32 bytes.
    "stp q0, q1, [x0, #(0 * 32)]\n"
    "stp q2, q3, [x0, #(1 * 32)]\n"
    "stp q4, q5, [x0, #(2 * 32)]\n"
    "stp q6, q7, [x0, #(3 * 32)]\n"
    "stp q8, q9, [x0, #(4 * 32)]\n"
    "stp q10, q11, [x0, #(5 * 32)]\n"
    "stp q12, q13, [x0, #(6 * 32)]\n"
    "stp q14, q15, [x0, #(7 * 32)]\n"
    "stp q16, q17, [x0, #(8 * 32)]\n"
    "stp q18, q19, [x0, #(9 * 32)]\n"
    "stp q20, q21, [x0, #(10 * 32)]\n"
    "stp q22, q23, [x0, #(11 * 32)]\n"
    "stp q24, q25, [x0, #(12 * 32)]\n"
    "stp q26, q27, [x0, #(13 * 32)]\n"
    "stp q28, q29, [x0, #(14* 32)]\n"
    "stp q30, q31, [x0, #(15 * 32)]\n"

    "bl zx_thread_exit\n"
    "brk 0\n"
    ".popsection\n");
#else
#error Unsupported architecture
#endif

// save_thread_local_regs_and_exit_thread() function.
#if defined(__x86_64__)
static_assert(offsetof(struct thread_local_regs, fs_base_value) == 8 * 0, "");
static_assert(offsetof(struct thread_local_regs, gs_base_value) == 8 * 1, "");
__asm__(
    ".pushsection .text,\"ax\", @progbits\n"
    ".global save_thread_local_regs_and_exit_thread\n"
    "save_thread_local_regs_and_exit_thread:\n"

    // Read from fs_base and gs_base into the output. Test will assert the
    // correct values were read.
    "movq %fs:0, %rax\n"
    "movq %rax, 8*0(%rsp)\n"
    "movq %gs:0, %rax\n"
    "movq %rax, 8*1(%rsp)\n"
    // Write constants into fs_base and gs_base. Test will assert the
    // correct values were written.
    "movq $0x12345678, %fs:0\n"
    "movq $0x7890abcd, %gs:0\n"

    "jmp zx_thread_exit@PLT\n"
    "ud2\n"
    ".popsection\n");
#elif defined(__aarch64__)
__asm__(
    ".pushsection .text,\"ax\", @progbits\n"
    ".global save_thread_local_regs_and_exit_thread\n"
    "save_thread_local_regs_and_exit_thread:\n"

    "mrs x1, tpidr_el0\n"
    "ldr x2, [x1]\n"
    "str x2, [sp, #(8*0)]\n"
    "movz x2, 0x5678\n"
    "movk x2, 0x1234, lsl 16\n"
    "str x2, [x1]\n"

    "bl zx_thread_exit\n"
    "brk 0\n"
    ".popsection\n");
#else
#error Unsupported architecture
#endif
