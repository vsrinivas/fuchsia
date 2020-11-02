// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_REGS_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_REGS_H_

#define X86_IFRAME_OFFSET_RDI (0 * 8)
#define X86_IFRAME_OFFSET_RSI (1 * 8)
#define X86_IFRAME_OFFSET_RBP (2 * 8)
#define X86_IFRAME_OFFSET_RBX (3 * 8)
#define X86_IFRAME_OFFSET_RDX (4 * 8)
#define X86_IFRAME_OFFSET_RCX (5 * 8)
#define X86_IFRAME_OFFSET_RAX (6 * 8)
#define X86_IFRAME_OFFSET_R8 (7 * 8)
#define X86_IFRAME_OFFSET_R9 (8 * 8)
#define X86_IFRAME_OFFSET_R10 (9 * 8)
#define X86_IFRAME_OFFSET_R11 (10 * 8)
#define X86_IFRAME_OFFSET_R12 (11 * 8)
#define X86_IFRAME_OFFSET_R13 (12 * 8)
#define X86_IFRAME_OFFSET_R14 (13 * 8)
#define X86_IFRAME_OFFSET_R15 (14 * 8)

#define X86_IFRAME_OFFSET_VECTOR (15 * 8)
#define X86_IFRAME_OFFSET_ERR_CODE (16 * 8)

#define X86_IFRAME_OFFSET_IP (17 * 8)
#define X86_IFRAME_OFFSET_CS (18 * 8)
#define X86_IFRAME_OFFSET_FLAGS (19 * 8)
#define X86_IFRAME_OFFSET_USER_SP (20 * 8)
#define X86_IFRAME_OFFSET_USER_SS (21 * 8)

#ifndef __ASSEMBLER__

#include <assert.h>
#include <stdint.h>

// Registers saved on entering the kernel via interrrupt or fault.
struct iframe_t {
  uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;     // pushed by common handler
  uint64_t r8, r9, r10, r11, r12, r13, r14, r15;  // pushed by common handler
  uint64_t vector;                                // pushed by stub
  uint64_t err_code;                              // pushed by interrupt or stub
  uint64_t ip, cs, flags;                         // pushed by interrupt
  uint64_t user_sp, user_ss;                      // pushed by interrupt
};

static_assert(offsetof(iframe_t, rdi) == X86_IFRAME_OFFSET_RDI, "");
static_assert(offsetof(iframe_t, rsi) == X86_IFRAME_OFFSET_RSI, "");
static_assert(offsetof(iframe_t, rbp) == X86_IFRAME_OFFSET_RBP, "");
static_assert(offsetof(iframe_t, rbx) == X86_IFRAME_OFFSET_RBX, "");
static_assert(offsetof(iframe_t, rdx) == X86_IFRAME_OFFSET_RDX, "");
static_assert(offsetof(iframe_t, rcx) == X86_IFRAME_OFFSET_RCX, "");
static_assert(offsetof(iframe_t, rax) == X86_IFRAME_OFFSET_RAX, "");
static_assert(offsetof(iframe_t, r8) == X86_IFRAME_OFFSET_R8, "");
static_assert(offsetof(iframe_t, r9) == X86_IFRAME_OFFSET_R9, "");
static_assert(offsetof(iframe_t, r10) == X86_IFRAME_OFFSET_R10, "");
static_assert(offsetof(iframe_t, r11) == X86_IFRAME_OFFSET_R11, "");
static_assert(offsetof(iframe_t, r12) == X86_IFRAME_OFFSET_R12, "");
static_assert(offsetof(iframe_t, r13) == X86_IFRAME_OFFSET_R13, "");
static_assert(offsetof(iframe_t, r14) == X86_IFRAME_OFFSET_R14, "");
static_assert(offsetof(iframe_t, r15) == X86_IFRAME_OFFSET_R15, "");

static_assert(offsetof(iframe_t, vector) == X86_IFRAME_OFFSET_VECTOR, "");
static_assert(offsetof(iframe_t, err_code) == X86_IFRAME_OFFSET_ERR_CODE, "");

static_assert(offsetof(iframe_t, ip) == X86_IFRAME_OFFSET_IP, "");
static_assert(offsetof(iframe_t, cs) == X86_IFRAME_OFFSET_CS, "");
static_assert(offsetof(iframe_t, flags) == X86_IFRAME_OFFSET_FLAGS, "");
static_assert(offsetof(iframe_t, user_sp) == X86_IFRAME_OFFSET_USER_SP, "");
static_assert(offsetof(iframe_t, user_ss) == X86_IFRAME_OFFSET_USER_SS, "");

// This structure holds the user general purpose integer registers during syscalls.
//
// It has a very specific layout and must be kept in sync with the assembly code in syscall.S.
struct syscall_regs_t {
  uint64_t rax;
  uint64_t rbx;
  uint64_t rcx;
  uint64_t rdx;
  uint64_t rsi;
  uint64_t rdi;
  uint64_t rbp;
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
  // rsp is the last field so we can simply "pop %rsp" when returning from a syscall.
  uint64_t rsp;
};

#endif  // !__ASSEMBLER__

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_REGS_H_
