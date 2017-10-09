// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <limits.h>

#define VMO_SIZE        0x200000
#define TRAP_PORT       0x11
#define TRAP_ADDR       (VMO_SIZE - PAGE_SIZE * 2)
#define EXIT_TEST_ADDR  (VMO_SIZE - PAGE_SIZE)

#define FUNCTION(x)     .global x; .type x,STT_FUNC; x:
