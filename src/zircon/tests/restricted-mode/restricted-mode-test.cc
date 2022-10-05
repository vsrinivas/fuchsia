// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/channel.h>
#include <lib/zx/resource.h>
#include <lib/zx/time.h>
#include <zircon/syscalls-next.h>
#include <zircon/syscalls.h>
#include <zircon/testonly-syscalls.h>
#include <zircon/types.h>

#include <pretty/hexdump.h>
#include <zxtest/zxtest.h>

#if __x86_64__
asm(R"(
.globl vectab
vectab:
  // back from restricted mode
  // rdi holds the context
  // rsi holds the error code
  mov  %rdi,%rsp
  pop  %rsp
  pop  %r15
  pop  %r14
  pop  %r13
  pop  %r12
  pop  %rbp
  pop  %rbx

  // pop the error code return slot
  pop  %rdx

  // return the error code from this function
  mov  %rax,(%rdx)

  // return back to whatever the address was on the stack
  // make it appear as if the wrapper had returned ZX_OK
  xor  %rax,%rax
  ret
)");

asm(R"(
.globl bounce
bounce:
  // do something to all the registers so we can read
  // the state on the way out
  inc  %rax
  inc  %rbx
  inc  %rcx
  inc  %rdx
  inc  %rsi
  inc  %rdi
  inc  %rbp
  inc  %rsp
  inc  %r8
  inc  %r9
  inc  %r10
  inc  %r11
  inc  %r12
  inc  %r13
  inc  %r14
  inc  %r15

  // write rcx and r11 to fs and gs base since they are both
  // trashed by the syscall. also tests that fs and gs base are
  // set properly.
  mov   %rcx, %fs:0
  mov   %r11, %gs:0

0:
  syscall #99
.globl bounce_post_syscall
bounce_post_syscall:
  jmp 0b
)");

asm(R"(
.globl restricted_enter_wrapper
restricted_enter_wrapper:
  // args 0 - 1 are already in place in rdi, rsi

  // save the return code pointer on the stack
  push  %rdx

  // save the callee saved regs since the return from restricted mode
  // will zero out all of the registers except rdi and rsi
  push  %rbx
  push  %rbp
  push  %r12
  push  %r13
  push  %r14
  push  %r15
  push  %rsp

  // save the pointer the stack as the context pointer in the syscall
  mov   %rsp,%rdx

  // call the syscall
  call  zx_restricted_enter

  // if we got here it must have failed
  add   $(8*8),%rsp // pop the previous state on the stack
  ret
)");

extern "C" void vectab();
extern "C" void bounce();
extern "C" void bounce_post_syscall();
extern "C" zx_status_t restricted_enter_wrapper(uint32_t options, uintptr_t vector_table,
                                                uint64_t *exit_code);

#endif  // __x86_64__

#if __x86_64__
TEST(RestrictedMode, Basic) {
  zx_status_t status;
  zx_restricted_state state{};

  // configure the state for x86
  uint64_t fs_val = 0;
  uint64_t gs_val = 0;
  state.ip = (uint64_t)bounce;
  state.flags = 0;
  state.rax = 0x0101010101010101;
  state.rbx = 0x0202020202020202;
  state.rcx = 0x0303030303030303;
  state.rdx = 0x0404040404040404;
  state.rsi = 0x0505050505050505;
  state.rdi = 0x0606060606060606;
  state.rbp = 0x0707070707070707;
  state.rsp = 0x0808080808080808;
  state.r8 = 0x0909090909090909;
  state.r9 = 0x0a0a0a0a0a0a0a0a;
  state.r10 = 0x0b0b0b0b0b0b0b0b;
  state.r11 = 0x0c0c0c0c0c0c0c0c;
  state.r12 = 0x0d0d0d0d0d0d0d0d;
  state.r13 = 0x0e0e0e0e0e0e0e0e;
  state.r14 = 0x0f0f0f0f0f0f0f0f;
  state.r15 = 0x1010101010101010;
  state.fs_base = (uintptr_t)&fs_val;
  state.gs_base = (uintptr_t)&gs_val;

  // set the state
  status = zx_restricted_write_state(&state, sizeof(state));
  ASSERT_EQ(ZX_OK, status);

  // enter restricted mode with reasonable args, expect a bounce back
  uint64_t exit_code = 99;
  status = restricted_enter_wrapper(0, (uintptr_t)vectab, &exit_code);
  ASSERT_EQ(ZX_OK, status);
  ASSERT_EQ(0, exit_code);

  // read the state out of the thread
  state = {};
  status = zx_restricted_read_state(&state, sizeof(state));
  ASSERT_EQ(ZX_OK, status);

  // validate that the instruction pointer is right after the syscall instruction
  EXPECT_EQ((uintptr_t)&bounce_post_syscall, state.ip);

  // validate the state of the registers is what was written inside restricted mode
  // NOTE: each of the registers was incremented by one before exiting restricted mode
  EXPECT_EQ(0x0101010101010102, state.rax);
  EXPECT_EQ(0x0202020202020203, state.rbx);
  EXPECT_EQ(0, state.rcx);  // RCX is trashed by the syscall and set to zero
  EXPECT_EQ(0x0404040404040405, state.rdx);
  EXPECT_EQ(0x0505050505050506, state.rsi);
  EXPECT_EQ(0x0606060606060607, state.rdi);
  EXPECT_EQ(0x0707070707070708, state.rbp);
  EXPECT_EQ(0x0808080808080809, state.rsp);
  EXPECT_EQ(0x090909090909090a, state.r8);
  EXPECT_EQ(0x0a0a0a0a0a0a0a0b, state.r9);
  EXPECT_EQ(0x0b0b0b0b0b0b0b0c, state.r10);
  EXPECT_EQ(0, state.r11);  // r11 is trashed by the syscall and set to zero
  EXPECT_EQ(0x0d0d0d0d0d0d0d0e, state.r12);
  EXPECT_EQ(0x0e0e0e0e0e0e0e0f, state.r13);
  EXPECT_EQ(0x0f0f0f0f0f0f0f10, state.r14);
  EXPECT_EQ(0x1010101010101011, state.r15);

  // validate that it was able to write to fs:0 and gs:0 while inside restricted mode
  // the post incremented values of rcx and r11 were written here
  EXPECT_EQ(0x0303030303030304, fs_val);
  EXPECT_EQ(0x0c0c0c0c0c0c0c0d, gs_val);
}
#endif  // __x86_64__

#if __x86_64__
TEST(RestrictedMode, Bench) {
  zx_status_t status;
  zx_restricted_state state{};

  // configure the state for x86
  uint64_t fs_val = 0;
  uint64_t gs_val = 0;
  state.ip = (uint64_t)bounce;
  state.flags = 0;
  state.fs_base = (uintptr_t)&fs_val;
  state.gs_base = (uintptr_t)&gs_val;

  // set the state
  status = zx_restricted_write_state(&state, sizeof(state));
  ASSERT_EQ(ZX_OK, status);

  // go through a full restricted syscall entry/exit cycle iter times and show the time
  constexpr int iter = 1000000;  // about a second worth of iterations on a mid range x86
  uint64_t exit_code;
  auto t = zx::ticks::now();
  for (int i = 0; i < iter; i++) {
    status = restricted_enter_wrapper(0, (uintptr_t)vectab, &exit_code);
    ASSERT_EQ(ZX_OK, status);
  }
  t = zx::ticks::now() - t;

  printf("restricted call %ld ns per round trip (%ld raw ticks)\n",
         t / iter * ZX_SEC(1) / zx::ticks::per_second(), t.get());

  // for way of comparison, time a null syscall
  t = zx::ticks::now();
  for (int i = 0; i < iter; i++) {
    status = zx_syscall_test_0();
    ASSERT_EQ(ZX_OK, status);
  }
  t = zx::ticks::now() - t;

  printf("test syscall %ld ns per call (%ld raw ticks)\n",
         t / iter * ZX_SEC(1) / zx::ticks::per_second(), t.get());
}
#endif  // __x86_64__

// test that restricted mode rejects invalid args restricted mode syscalls
#if __x86_64__
TEST(RestrictedMode, InvalidState) {
  zx_status_t status;
  zx_restricted_state state{};

  __UNUSED auto set_state_and_enter = [&]() {
    // set the state
    status = zx_restricted_write_state(&state, sizeof(state));
    ASSERT_EQ(ZX_OK, status);

    // this should fail with bad state
    status = zx_restricted_enter(0, (uintptr_t)&vectab, 0);
    ASSERT_EQ(ZX_ERR_BAD_STATE, status);
  };

  state = {};
  state.ip = -1;  // ip is outside of user space
  set_state_and_enter();

  state = {};
  state.ip = (uint64_t)bounce;
  state.flags = (1UL << 31);  // set an invalid flag
  set_state_and_enter();

  state = {};
  state.ip = (uint64_t)bounce;
  state.fs_base = (1UL << 63);  // invalid fs (non canonical)
  set_state_and_enter();

  state = {};
  state.ip = (uint64_t)bounce;
  state.gs_base = (1UL << 63);  // invalid gs (non canonical)
  set_state_and_enter();
}
#endif  // __x86_64__

#if __aarch64__
TEST(RestrictedMode, UnimplementedOnArm) {
  zx_status_t status;
  zx_restricted_state state;

  // currently the enter syscall is unimplemented on ARM, so make sure it fails as we expect

  // set a null state which should pass
  state = {};
  status = zx_restricted_write_state(&state, sizeof(state));
  EXPECT_EQ(ZX_OK, status);

  // make sure enter fails
  static int vector;
  status = zx_restricted_enter(0, (uintptr_t)&vector, 0);
  EXPECT_EQ(ZX_ERR_BAD_STATE, status);
}

#endif  // __aarch64__

TEST(RestrictedMode, InvalidArgs) {
  zx_status_t status;

  // enter restricted mode with invalid args
  status = zx_restricted_enter(0, -1, 0);  // vector table must be valid user pointer
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);

  status = zx_restricted_enter(0xffffffff, 0, 0);  // flags must be zero
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);

  // size of read/write state must match sizeof(zx_restricted_state)
  status = zx_restricted_read_state(nullptr, 0);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);
  status = zx_restricted_write_state(nullptr, 0);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);

  status = zx_restricted_read_state(nullptr, sizeof(zx_restricted_state) - 1);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);
  status = zx_restricted_write_state(nullptr, sizeof(zx_restricted_state) - 1);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);

  status = zx_restricted_read_state(nullptr, sizeof(zx_restricted_state) + 1);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);
  status = zx_restricted_write_state(nullptr, sizeof(zx_restricted_state) + 1);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);
}
