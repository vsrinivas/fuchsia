// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stddef.h>
#include <stdint.h>
#include <zircon/assert.h>

#include "test-main.h"

const char Symbolize::kProgramName_[] = "start-test";

namespace {

constexpr size_t kStackAlignment = 16;

bool StackAligned(void* ptr) {
  // Make sure the compiler doesn't think it knows the value,
  // so there will be a runtime check.
  __asm__("nop" : "=r"(ptr) : "0"(ptr));
  return reinterpret_cast<uintptr_t>(ptr) % kStackAlignment == 0;
}

}  // namespace

int TestMain(void*, arch::EarlyTicks) {
  void* machine_stack = __builtin_frame_address(0);
  ZX_ASSERT(StackAligned(machine_stack));

#if __has_feature(safe_stack)
  alignas(kStackAlignment) char unsafe_stack[1] = {17};
  ZX_ASSERT(StackAligned(unsafe_stack));
#endif

  return 0;
}
