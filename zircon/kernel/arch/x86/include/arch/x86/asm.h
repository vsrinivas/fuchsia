// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_ASM_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_ASM_H_

#include <asm.h>

/* x86 assembly macros used in a few files */

#define PHYS_LOAD_ADDRESS (KERNEL_LOAD_OFFSET)
#define PHYS_ADDR_DELTA (KERNEL_BASE - PHYS_LOAD_ADDRESS)
#define PHYS(x) ((x)-PHYS_ADDR_DELTA)

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_X86_ASM_H_
