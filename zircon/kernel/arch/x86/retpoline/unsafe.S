// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/asm.h>

// "Unsafe" trampoline, used when speculative execution mitigations are
// disabled or when we rely on IBRS for mitigation.
.function __x86_indirect_thunk_unsafe_r11, global
  jmp *%r11
.end_function
