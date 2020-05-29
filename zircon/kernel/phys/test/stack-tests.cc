// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>
#include <stddef.h>
#include <stdint.h>

namespace {

constexpr size_t kStackAlignment = 16;

// The frame pointer is the SP after pushing two words.
// This differs mod 16 when using 32-bit words (x86-32).
constexpr size_t kFpAdjust = 2 * sizeof(uintptr_t);

bool StackAligned(void* ptr, bool fp) {
  // Make sure the compiler doesn't think it knows the value,
  // so there will be a runtime check.
  uintptr_t stack_addr;
  __asm__("" : "=g"(stack_addr) : "0"(ptr));
  if (fp) {
    stack_addr += kFpAdjust;
  }
  return stack_addr % kStackAlignment == 0;
}

bool stack_alignment() {
  BEGIN_TEST;

  void* machine_stack = __builtin_frame_address(0);
  EXPECT_TRUE(StackAligned(machine_stack, true));

#if __has_feature(safe_stack)
  alignas(kStackAlignment) char unsafe_stack[1] = {17};
  EXPECT_TRUE(StackAligned(unsafe_stack, false));
#endif

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(stack_tests)
UNITTEST("stack alignment", stack_alignment)
UNITTEST_END_TESTCASE(stack_tests, "stack", "stack tests")
