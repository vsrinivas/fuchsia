// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdio.h>
#include <zircon/assert.h>

#include "../frame-pointer.h"
#include "../symbolize.h"
#include "test-main.h"

const char Symbolize::kProgramName_[] = "backtrace-test";

namespace {

// The backtrace omits its immediate caller, so Collect won't appear.
[[gnu::noinline]] auto Collect() { return FramePointer::BackTrace(); }

[[gnu::noinline]] PHYS_SINGLETHREAD int Find() {
  const auto bt = Collect();

  int depth = 0;
  for ([[maybe_unused]] auto pc : bt) {
    ++depth;
  }

  printf("Printing backtrace, %d frames:\n", depth);

  Symbolize::GetInstance()->BackTrace(bt);

  return depth - 1;
}

[[gnu::noinline]] PHYS_SINGLETHREAD int Outer() { return Find() - 1; }

[[gnu::noinline]] PHYS_SINGLETHREAD int Otter() { return Outer() - 1; }

[[gnu::noinline]] PHYS_SINGLETHREAD int Foo() { return Otter() - 1; }

}  // namespace

int TestMain(void*, arch::EarlyTicks) {
  ZX_ASSERT(Foo() == 3);  // _start -> PhysMain -> TestMain -> Foo...
  return 0;
}
