// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdio.h>
#include <zircon/assert.h>

#include <phys/allocation.h>
#include <phys/arch.h>
#include <phys/symbolize.h>

#include "legacy-boot.h"
#include "test-main.h"

const char Symbolize::kProgramName_[] = "paging-test";

int TestMain(void* ptr, arch::EarlyTicks) {
  InitMemory(ptr);

  EnablePaging();

  static volatile int datum = 17;
  ZX_ASSERT(datum == 17);
  datum = 23;
  ZX_ASSERT(datum == 23);

  // If we're still here, virtual memory works.
  printf("Hello virtual world!\n");

  return 0;
}
