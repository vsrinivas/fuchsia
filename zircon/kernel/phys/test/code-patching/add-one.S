// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/asm.h>
#include <lib/code-patching/asm.h>

#include "test.h"

.text

// uint64_t AddOne(uint64_t x).
.function AddOne, global
#if defined(__aarch64__)
0:
  add x0, x0, #1
#elif defined(__x86_64__)
  mov %rdi, %rax
0:
  add $1, %rax
#else
  .error "unknown architecture"
#endif
1:
  .ifne 1b - 0b - PATCH_SIZE_ADD_ONE
  .error "wrong size"
  .endif

  .code_patching.range 0b, 1b, CASE_ID_ADD_ONE
  ret
.end_function
