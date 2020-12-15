// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_STACK_H_
#define ZIRCON_KERNEL_PHYS_STACK_H_

#include <zircon/compiler.h>

#define BOOT_STACK_ALIGN 16
#define BOOT_STACK_SIZE 16384

#if __has_feature(shadow_call_stack)
#define BOOT_SHADOW_CALL_STACK_SIZE 512
#endif  // __has_feature(shadow_call_stack)

#ifndef __ASSEMBLER__
#include <stdint.h>

struct BootStack {
  alignas(BOOT_STACK_ALIGN) uint8_t stack[BOOT_STACK_SIZE];
  static_assert(BOOT_STACK_SIZE % BOOT_STACK_ALIGN == 0);
};

extern BootStack boot_stack;

// Returns true iff SP falls on the boot stack.
// This considers the limit to be "on".
bool IsOnStack(uintptr_t sp);

#if __has_feature(safe_stack)
extern BootStack boot_unsafe_stack;
#endif  // __has_feature(safe_stack)

#if __has_feature(shadow_call_stack)
static_assert(BOOT_SHADOW_CALL_STACK_SIZE % sizeof(uintptr_t) == 0);
extern uintptr_t boot_shadow_call_stack[BOOT_SHADOW_CALL_STACK_SIZE / sizeof(uintptr_t)];
#endif  // __has_feature(shadow_call_stack)

#endif  // __ASSEMBLER__

#endif  // ZIRCON_KERNEL_PHYS_STACK_H_
