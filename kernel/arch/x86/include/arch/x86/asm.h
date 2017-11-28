// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <asm.h>

/* x86 assembly macros used in a few files */

#define PHYS_LOAD_ADDRESS (KERNEL_LOAD_OFFSET)
#define PHYS_ADDR_DELTA (KERNEL_BASE - PHYS_LOAD_ADDRESS)
#define PHYS(x) ((x) - PHYS_ADDR_DELTA)
