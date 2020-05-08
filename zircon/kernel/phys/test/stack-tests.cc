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

bool StackAligned(void* ptr) {
  // Make sure the compiler doesn't think it knows the value,
  // so there will be a runtime check.
  __asm__("nop" : "=r"(ptr) : "0"(ptr));
  return reinterpret_cast<uintptr_t>(ptr) % kStackAlignment == 0;
}

bool stack_alignment() {
  BEGIN_TEST;

  void* machine_stack = __builtin_frame_address(0);
  EXPECT_TRUE(StackAligned(machine_stack));

#if __has_feature(safe_stack)
  alignas(kStackAlignment) char unsafe_stack[1] = {17};
  EXPECT_TRUE(StackAligned(unsafe_stack));
#endif

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(stack_tests)
UNITTEST("stack alignment", stack_alignment)
UNITTEST_END_TESTCASE(stack_tests, "stack", "stack tests")
