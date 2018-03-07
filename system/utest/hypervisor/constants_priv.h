// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <limits.h>

// clang-format off

#define VMO_SIZE        0x1000000
#define TRAP_PORT       0x11
#define TRAP_ADDR       (VMO_SIZE - PAGE_SIZE * 2)
#define EXIT_TEST_ADDR  (VMO_SIZE - PAGE_SIZE)

#if __x86_64__
#define GUEST_ENTRY     0x2000
#define X86_CR0_NE      0x00000020 /* enable x87 exception */
#endif

#define FUNCTION(x)     .global x; .type x,STT_FUNC; x:
