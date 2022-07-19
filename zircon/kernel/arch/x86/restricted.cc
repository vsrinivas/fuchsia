// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch.h>
#include <inttypes.h>
#include <stdlib.h>
#include <trace.h>

#include <arch/regs.h>
#include <arch/vm.h>
#include <arch/x86.h>
#include <arch/x86/descriptor.h>
#include <kernel/restricted.h>

#define LOCAL_TRACE 0

void X86ArchRestrictedState::Dump() {
  printf("ArchRestrictedState %p:\n", this);
  printf(" RIP: %#18" PRIx64 "  FL: %#18" PRIx64 "\n", state_.ip, state_.flags);
  printf(" RAX: %#18" PRIx64 " RBX: %#18" PRIx64 " RCX: %#18" PRIx64 " RDX: %#18" PRIx64 "\n",
         state_.rax, state_.rbx, state_.rcx, state_.rdx);
  printf(" RSI: %#18" PRIx64 " RDI: %#18" PRIx64 " RBP: %#18" PRIx64 " RSP: %#18" PRIx64 "\n",
         state_.rsi, state_.rdi, state_.rbp, state_.rsp);
  printf("  R8: %#18" PRIx64 "  R9: %#18" PRIx64 " R10: %#18" PRIx64 " R11: %#18" PRIx64 "\n",
         state_.r8, state_.r9, state_.r10, state_.r11);
  printf(" R12: %#18" PRIx64 " R13: %#18" PRIx64 " R14: %#18" PRIx64 " R15: %#18" PRIx64 "\n",
         state_.r12, state_.r13, state_.r14, state_.r15);
  printf("fs base %#18" PRIx64 " gs base %#18" PRIx64 "\n", state_.fs_base, state_.gs_base);
}

bool X86ArchRestrictedState::ValidatePreRestrictedEntry() {
  // validate that RIP is within user space
  if (!is_user_accessible(state_.ip)) {
    TRACEF("fail due to bad ip %#" PRIx64 "\n", state_.ip);
    return false;
  }

  // validate that the rflags saved only contain user settable flags
  if ((state_.flags & ~X86_FLAGS_USER) != 0) {
    TRACEF("fail due to flags outside of X86_FLAGS_USER set (%#" PRIx64 ")\n", state_.flags);
    return false;
  }

  // fs and gs base must be canonical
  if (!x86_is_vaddr_canonical(state_.fs_base)) {
    TRACEF("fail due to bad fs base %#" PRIx64 "\n", state_.fs_base);
    return false;
  }
  if (!x86_is_vaddr_canonical(state_.gs_base)) {
    TRACEF("fail due to bad gs base %#" PRIx64 "\n", state_.gs_base);
    return false;
  }

  // everything else can be whatever value it wants to be. worst case it immediately faults
  // in restricted mode and that's okay.
  return true;
}

void X86ArchRestrictedState::SaveStatePreRestrictedEntry() {
  // save the normal mode fs/gs base which we'll reload on the way back
  normal_fs_base_ = read_msr(X86_MSR_IA32_FS_BASE);
  normal_gs_base_ = read_msr(X86_MSR_IA32_KERNEL_GS_BASE);
}

void X86ArchRestrictedState::EnterRestricted() {
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(x86_is_vaddr_canonical(state_.fs_base));
  DEBUG_ASSERT(x86_is_vaddr_canonical(state_.gs_base));

  // load the user fs/gs base from restricted mode
  write_msr(X86_MSR_IA32_FS_BASE, state_.fs_base);
  write_msr(X86_MSR_IA32_KERNEL_GS_BASE, state_.gs_base);

  // copy to a kernel iframe_t
  // struct iframe_t {
  //   uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;     // pushed by common handler
  //   uint64_t r8, r9, r10, r11, r12, r13, r14, r15;  // pushed by common handler
  //   uint64_t vector;                                // pushed by stub
  //   uint64_t err_code;                              // pushed by interrupt or stub
  //   uint64_t ip, cs, flags;                         // pushed by interrupt
  //   uint64_t user_sp, user_ss;                      // pushed by interrupt
  // };
  iframe_t iframe{};
  iframe.rdi = state_.rdi;
  iframe.rsi = state_.rsi;
  iframe.rbp = state_.rbp;
  iframe.rbx = state_.rbx;
  iframe.rdx = state_.rdx;
  iframe.rcx = state_.rcx;
  iframe.rax = state_.rax;
  iframe.r8 = state_.r8;
  iframe.r9 = state_.r9;
  iframe.r10 = state_.r10;
  iframe.r11 = state_.r11;
  iframe.r12 = state_.r12;
  iframe.r13 = state_.r13;
  iframe.r14 = state_.r14;
  iframe.r15 = state_.r15;
  iframe.ip = state_.ip;
  iframe.cs = USER_CODE_64_SELECTOR;
  iframe.flags = state_.flags | X86_FLAGS_IF;
  iframe.user_sp = state_.rsp;
  iframe.user_ss = USER_DATA_SELECTOR;
  iframe.vector = iframe.err_code = 0;  // iframe.vector/err_code are unused

  // load the new state and exit
  arch_enter_uspace(&iframe);

  __UNREACHABLE;
}

void X86ArchRestrictedState::SaveRestrictedSyscallState(const syscall_regs_t *regs) {
  // copy state syscall_regs_t to zx_restricted_state
  state_.rdi = regs->rdi;
  state_.rsi = regs->rsi;
  state_.rbp = regs->rbp;
  state_.rbx = regs->rbx;
  state_.rdx = regs->rdx;
  state_.rcx = regs->rcx;
  state_.rax = regs->rax;
  state_.rsp = regs->rsp;
  state_.r8 = regs->r8;
  state_.r9 = regs->r9;
  state_.r10 = regs->r10;
  state_.r11 = regs->r11;
  state_.r12 = regs->r12;
  state_.r13 = regs->r13;
  state_.r14 = regs->r14;
  state_.r15 = regs->r15;
  state_.ip = regs->rip;
  state_.flags = regs->rflags & X86_FLAGS_USER;

  // read the fs/gs base out of the MSRs
  state_.fs_base = read_msr(X86_MSR_IA32_FS_BASE);
  state_.gs_base = read_msr(X86_MSR_IA32_KERNEL_GS_BASE);
}

void X86ArchRestrictedState::EnterFull(uintptr_t vector_table, uintptr_t context, uint64_t code) {
  // load the user fs/gs base from normal mode
  DEBUG_ASSERT(x86_is_vaddr_canonical(normal_fs_base_));
  DEBUG_ASSERT(x86_is_vaddr_canonical(normal_gs_base_));
  write_msr(X86_MSR_IA32_FS_BASE, normal_fs_base_);
  write_msr(X86_MSR_IA32_KERNEL_GS_BASE, normal_gs_base_);

  // set up a mostly blank iframe and return back to normal mode
  iframe_t iframe{};
  iframe.rdi = context;
  iframe.rsi = code;
  iframe.ip = vector_table;
  iframe.cs = USER_CODE_64_SELECTOR;
  iframe.flags = X86_FLAGS_IF;
  iframe.user_ss = USER_DATA_SELECTOR;

  // load the new state and exit
  arch_enter_uspace(&iframe);

  __UNREACHABLE;
}
