// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

.globl restricted_enter
restricted_enter:
  // args 0 - 1 are already in place in rdi, rsi

  // save the callee saved regs
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
  add   $(7*8),%rsp // pop the previous state on the stack
  ret

.globl restricted_return
restricted_return:
  // back from restricted mode, rdi holds the context
  mov  %rdi,%rsp
  pop  %rsp
  pop  %r15
  pop  %r14
  pop  %r13
  pop  %r12
  pop  %rbp
  pop  %rbx

  // return back to whatever the address was on the stack
  ret
