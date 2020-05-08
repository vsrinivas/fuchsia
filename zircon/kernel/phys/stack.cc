// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "stack.h"

BootStack boot_stack;

#if __has_feature(safe_stack)
BootStack boot_unsafe_stack;
#endif  // __has_feature(safe_stack)

#if __has_feature(shadow_call_stack)
uintptr_t boot_shadow_call_stack[BOOT_SHADOW_CALL_STACK_SIZE / sizeof(uintptr_t)];
#endif  // __has_feature(shadow_call_stack)

// This considers the limit to be "on".
bool IsOnStack(uintptr_t sp) {
  const auto base = reinterpret_cast<uintptr_t>(&boot_stack);
  return base <= sp && sp - base <= sizeof(boot_stack);
}
