// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdio.h>

// This program is setup so that it needs to have registers written at key
// points so that it passes successfully.
//
// Scenarios:
//
// 1. RAX branch

static bool passed_ok = false;

extern "C" {
  void Success() {
    passed_ok = true;
  }

  void Failure() {
    assert(false);
  }

  void BranchOnRAX();
}

// RAX Branch
//
// This will hardcode a SW breakpoint just before comparing RAX to 0. If 0, it
// will call Failure. The debug agent must write RAX at this point in order for
// the software to call Success.
__asm__(R"(
  .pushsection .text, "ax", @progbits
  .global BranchOnRAX

BranchOnRAX:
    /* Function preamble. */
    pushq %rbp
    movq %rsp, %rbp

    movq $0, %rax
    int $3

    /* Compare variable set here. */
    /* Changing RAX != 1 will branch to the success case. */
    cmp $0, %rax

    je .CALL_FAILURE
    call Success()
    jmp .END

.CALL_FAILURE:
    call Failure()

.END:
    nop
    leave
    ret
  .popsection)");

int main() {
  BranchOnRAX();

  return passed_ok ? 0 : 1;
}
