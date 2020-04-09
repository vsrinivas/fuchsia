// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stddef.h>
#include <stdint.h>
#include <zircon/assert.h>

#include "../main.h"

namespace {

constexpr size_t kStackAlignment = 16;

bool StackAligned(void* ptr) {
  // Make sure the compiler doesn't think it knows the value,
  // so there will be a runtime check.
  __asm__("nop" : "=r"(ptr) : "0"(ptr));
  return reinterpret_cast<uintptr_t>(ptr) % kStackAlignment == 0;
}

[[gnu::noinline, noreturn]] void Spin() {
  while (true) {
    // Do something the compiler doesn't recognize as "nothing".  In C++ an
    // infinite loop that does nothing is technically undefined behavior and
    // so the compiler is allowed to do silly things.
    __asm__ volatile("");
  }
}

}  // namespace

void PhysMain(void*, ArchEarlyTicks) {
  void* machine_stack = __builtin_frame_address(0);
  ZX_ASSERT(StackAligned(machine_stack));

#if __has_feature(safe_stack)
  alignas(kStackAlignment) char unsafe_stack[1] = {17};
  ZX_ASSERT(StackAligned(unsafe_stack));
#endif

  // TODO(46879): There's no I/O or shutdown implemented yet, so this is
  // "tested" just by observing in the debugger that it's spinning here
  // and didn't crash or assert first.  When serial output is working,
  // this will morph into a "hello world" test and later be replaced
  // or augmented by a variety of test programs.
  Spin();
}
