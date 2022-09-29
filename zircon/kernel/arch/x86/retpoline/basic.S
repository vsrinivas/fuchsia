// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/asm.h>

// Generic x86 retpoline implementation
.function __x86_indirect_thunk_basic_r11, global
  call .LINT1
.LINT0:
  pause
  lfence
  jmp .LINT0
.LINT1:
  movq %r11, (%rsp)
  ret
  int3
.end_function
