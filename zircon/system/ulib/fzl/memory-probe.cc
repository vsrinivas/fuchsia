// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fzl/memory-probe.h>
#include <lib/zx/channel.h>
#include <lib/zx/exception.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <limits.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>

namespace {

// These are not really functions, but entry points for a thread that has a
// tiny stack and no other setup.  They're not really entered with the C
// ABI as such.  Rather, they're entered with the first argument register
// set to an address and with the SP at the very top of the allocated
// stack.  They're defined in pure assembly so that there are no issues
// with compiler-generated code's assumptions about the proper ABI setup,
// instrumentation, etc.
//
// Since this calls into the vDSO, it must adhere to the vDSO's ABI, which is
// the "vanilla" C calling convention (no safe-stack or shadow-call-stack).
// As well as the register usage conventions, this mandates a stack of some
// reasonable minimum size, even on AArch64 where the calling convention
// doesn't per se involve the stack (but it is specified that the SP must be
// "valid" on function entry).  Today's vDSO implementation might not actually
// make use of the stack in the zx_thread_exit call, but it always could.  The
// x86 C calling convention mandates that the stack pointer have exactly the
// alignment it gets from the call instruction on an aligned stack (that is,
// SP % 16 == 8).
extern "C" void read_thread_func(uintptr_t address, uintptr_t);
extern "C" void write_thread_func(uintptr_t address, uintptr_t);

#define PROBE_FUNC(name, insn)                               \
  __asm__(".pushsection .text." #name                        \
          ",\"ax\",%progbits\n"                              \
          ".balign 4\n"                                      \
          ".type " #name                                     \
          ",%function\n"                                     \
          ".cfi_startproc\n" #name ":\n" insn "\n" CALL_INSN \
          " zx_thread_exit\n"                                \
          ".cfi_endproc\n"                                   \
          ".size " #name ", . - " #name                      \
          "\n"                                               \
          ".popsection");

#ifdef __aarch64__
#define CALL_INSN "bl"
#define READ_PROBE_INSN "ldrb w1, [x0]"
#define WRITE_PROBE_INSN "strb wzr, [x0]"
#elif defined(__x86_64__)
#define CALL_INSN "call"
#define READ_PROBE_INSN "movb (%rdi), %al"
#define WRITE_PROBE_INSN "movb %al, (%rdi)"
#elif defined(__riscv)
#define CALL_INSN "jal"
#define READ_PROBE_INSN ""
#define WRITE_PROBE_INSN ""
#else
#error "what machine?"
#endif

PROBE_FUNC(read_thread_func, READ_PROBE_INSN)
PROBE_FUNC(write_thread_func, WRITE_PROBE_INSN)

zx_status_t advance_program_counter(const zx::thread& thread) {
  zx_thread_state_general_regs_t regs;
  auto status = thread.read_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs));
  if (status != ZX_OK) {
    return status;
  }

#if defined(__aarch64__)
  regs.pc += 4u;
#elif defined(__x86_64__)
  regs.rip += 2u;
#elif defined(__riscv)
#else
#error "what machine?"
#endif

  return thread.write_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs));
}

bool do_probe(void (*op)(uintptr_t address, uintptr_t), uintptr_t addr) {
  // This function starts a new thread to perform the read/write test, and catches any exceptions
  // in this thread to see if it failed or not.
  zx::thread thread;
  ZX_ASSERT(zx::thread::create(*zx::process::self(), "memory_probe", 12u, 0u, &thread) == ZX_OK);

  alignas(16) static uint8_t thread_stack[128];
  void* stack = thread_stack + sizeof(thread_stack);

  zx::channel exception_channel;
  ZX_ASSERT(thread.create_exception_channel(0, &exception_channel) == ZX_OK);

  thread.start(op, stack, addr, 0);

  // Wait for exception or thread completion.
  zx_signals_t signals = 0;
  ZX_ASSERT(exception_channel.wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                       zx::time::infinite(), &signals) == ZX_OK);

  if (signals & ZX_CHANNEL_READABLE) {
    zx_exception_info_t info;
    zx::exception exception;
    ZX_ASSERT(exception_channel.read(0, &info, exception.reset_and_get_address(), sizeof(info), 1,
                                     nullptr, nullptr) == ZX_OK);
    ZX_ASSERT(info.type == ZX_EXCP_FATAL_PAGE_FAULT);
    ZX_ASSERT(advance_program_counter(thread) == ZX_OK);
    uint32_t state = ZX_EXCEPTION_STATE_HANDLED;
    exception.set_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state));
    return false;
  }

  // Thread terminated normally so the memory is readable/writable.
  return true;
}

}  // namespace

bool probe_for_read(const void* addr) {
  return do_probe(read_thread_func, reinterpret_cast<uintptr_t>(addr));
}

bool probe_for_write(void* addr) {
  return do_probe(write_thread_func, reinterpret_cast<uintptr_t>(addr));
}
