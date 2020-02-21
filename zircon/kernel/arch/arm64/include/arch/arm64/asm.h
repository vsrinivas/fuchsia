// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_ASM_H_
#define ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_ASM_H_

#include <asm.h>

// The kernel is compiled using -ffixed-x15 so the compiler will never use
// this register.
percpu_ptr .req x15

// This register is permanently reserved by the ABI in the compiler.
// #if __has_feature(shadow_call_stack) it's used for the SCSP.
shadow_call_sp .req x18

#endif  // ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_ASM_H_
