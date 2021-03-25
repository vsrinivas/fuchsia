// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/asm.h>

// AMD-specific trampolines
// "Software Techniques for Managing Speculation on AMD Processors", Mitigation V2-2
.function __x86_indirect_thunk_amd_r11, global
  lfence
  jmp *%r11
.end_function
