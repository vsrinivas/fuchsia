// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdio.h>

#include "test-main.h"

#if defined(__x86_64__) || defined(__i386__)
#include <lib/arch/x86/boot-cpuid.h>
#endif

const char Symbolize::kProgramName_[] = "hello-world-test";

int TestMain(void*, arch::EarlyTicks) {
  printf("Hello, world!\n");

#if defined(__x86_64__) || defined(__i386__)
  printf("CPU features:\n");
  constexpr auto print_one = [](const char* s) { printf("\t%s\n", s); };
  arch::BootCpuid<arch::CpuidFeatureFlagsC>().Print(print_one);
  arch::BootCpuid<arch::CpuidFeatureFlagsD>().Print(print_one);
  arch::BootCpuid<arch::CpuidExtendedFeatureFlagsB>().Print(print_one);
#endif

  return 0;
}
