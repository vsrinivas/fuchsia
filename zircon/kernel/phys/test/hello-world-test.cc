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
  constexpr auto print_feature = [](const char* name, auto value, auto high_bit, auto low_bit) {
    if (name && value) {
      printf("\t%s\n", name);
    }
  };
  arch::BootCpuid<arch::CpuidFeatureFlagsC>().ForEachField(print_feature);
  arch::BootCpuid<arch::CpuidFeatureFlagsD>().ForEachField(print_feature);
  arch::BootCpuid<arch::CpuidExtendedFeatureFlagsB>().ForEachField(print_feature);
#endif

  return 0;
}
