// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/asm.h>

.text

// uint64_t multiply_by_ten(uint64_t x).
.function multiply_by_ten, global
#if defined(__aarch64__)
  add x0, x0, x0, lsl #2
  lsl x0, x0, #1
#elif defined(__x86_64__)
  add %rdi, %rdi
  lea (%rdi, %rdi, 4), %rax
#else
  .error "unknown architecture"
#endif
  ret
.end_function
