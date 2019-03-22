// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/test_data/test_so_symbols.h"

#include <assert.h>
#include <stdio.h>

// TODO(donosoc): Move this to test_so_common.cc
bool gTestPassed = false;

extern "C" {
void Success() {
  printf("Test passes :)\n");
  gTestPassed = true;
}

void Failure() {
  printf("Test failed :(\n");
  gTestPassed = false;
  assert(false);
}
}

// Common routines

__asm__(R"(
  .pushsection .text, "ax", @progbits

.END:
    nop
    leave
    ret

  .popsection
)");

// RAX Branch ------------------------------------------------------------------
//
// This will hardcode a SW breakpoint just before comparing RAX to 0. If 0, it
// will call Failure. The debug agent must write RAX at this point in order for
// the software to call Success.
__asm__(R"(
  .pushsection .text, "ax", @progbits
  .global Test_BranchOnRAX

Test_BranchOnRAX:
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

  .popsection
)");

// PC Jump ---------------------------------------------------------------------
//
// This tests inserts a SW breakpoint before failing the test.
// In order to be successful, the debugger must jump (set the PC) to a known
// label (PC_Target), which will call success.

__asm__(R"(
  .pushsection .text, "ax", @progbits
  .global Test_PCJump
  /* Also export the place where you need to jump to. */
  .global PC_Target

Test_PCJump:
  /* Function preamble. */
  pushq %rbp
  movq %rsp, %rbp

  /* We insert a SW breakpoint. */
  int $3

  call Failure()
  jmp .END

PC_Target:
  call Success()
  jmp .END

  .popsection
)");
