// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <phys/frame-pointer.h>
#include <phys/stack.h>
#include <phys/symbolize.h>

#include "test-main.h"

const char Symbolize::kProgramName_[] = "backtrace-test";

namespace {

// BackTrace() omits its immediate caller, so Collect* itself won't appear.

[[gnu::noinline]] auto CollectFp() { return FramePointer::BackTrace(); }

[[gnu::noinline]] auto CollectScs() { return boot_shadow_call_stack.BackTrace(); }

[[gnu::noinline]] PHYS_SINGLETHREAD int Find() {
  constexpr auto bt_depth = [](auto&& bt) {
    int depth = 0;
    for ([[maybe_unused]] auto pc : bt) {
      ++depth;
    }
    return depth;
  };

  printf("Collecting backtraces...\n");
  Symbolize& symbolize = *Symbolize::GetInstance();
  symbolize.Context();

  const auto fp_bt = CollectFp();
  const int fp_depth = bt_depth(fp_bt);

  printf("Printing frame pointer backtrace, %d frames:\n", fp_depth);
  symbolize.BackTrace(fp_bt);

  const auto scs_bt = CollectScs();
  const int scs_depth = bt_depth(scs_bt);
  if (BootShadowCallStack::kEnabled) {
    printf("Printing shadow call stack backtrace, %d frames:\n", scs_depth);
    symbolize.BackTrace(scs_bt);

    ZX_ASSERT(fp_depth == scs_depth);

    struct Both {
      decltype(fp_bt.begin()) fp;
      decltype(scs_bt.begin()) scs;
      bool first = true;
    };
    for (auto [fp, scs, first] = Both{fp_bt.begin(), scs_bt.begin()}; fp != fp_bt.end();
         ++fp, ++scs, first = false) {
      ZX_ASSERT(scs != scs_bt.end());

      // The first PC is the collection call site above, which differs between
      // the two collections.  The rest should match.
      if (first) {
        ZX_ASSERT(*scs != *fp);
      } else {
        ZX_ASSERT(*scs == *fp);
      }
    }
  } else {
    ZX_ASSERT(scs_bt.empty());
    ZX_ASSERT(scs_depth == 0);
  }

  return fp_depth - 1;
}

[[gnu::noinline]] PHYS_SINGLETHREAD int Outer() { return Find() - 1; }

[[gnu::noinline]] PHYS_SINGLETHREAD int Otter() { return Outer() - 1; }

[[gnu::noinline]] PHYS_SINGLETHREAD int Foo() { return Otter() - 1; }

}  // namespace

int TestMain(void* zbi, arch::EarlyTicks) {
  if (zbi && static_cast<zbi_header_t*>(zbi)->type == ZBI_TYPE_CONTAINER) {
    ZX_ASSERT(Foo() == 4);  // _start -> PhysMain -> ZbiMain -> TestMain -> Foo
  } else {
    ZX_ASSERT(Foo() == 3);  // _start -> PhysMain -> TestMain -> Foo...
  }
  return 0;
}
