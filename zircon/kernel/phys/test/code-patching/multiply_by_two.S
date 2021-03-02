// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/asm.h>

.text

// uint64_t multiply_by_two(uint64_t x).
.function multiply_by_two, global
#if defined(__aarch64__)
  lsl x0, x0, #1
#elif defined(__x86_64__)
  lea (%rdi, %rdi), %rax
#else
  .error "unknown architecture"
#endif
  ret
.end_function
