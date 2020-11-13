// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdio.h>

#include <string_view>

#include "test-main.h"

#if defined(__x86_64__) || defined(__i386__)
#include <lib/arch/x86/boot-cpuid.h>
#endif

const char Symbolize::kProgramName_[] = "hello-world-test";

int TestMain(void*, arch::EarlyTicks) {
  printf("Hello, world!\n\n");

#if defined(__x86_64__) || defined(__i386__)
  arch::BootCpuidIo io;

  {
    arch::ProcessorName processor(io);
    std::string_view name = processor.name();
    printf("Processor: %.*s\n", static_cast<int>(name.size()), name.data());
  }

  {
    arch::HypervisorName hypervisor(io);
    std::string_view name = hypervisor.name();
    name = name.empty() ? "None" : name;
    printf("Hypervisor: %.*s\n", static_cast<int>(name.size()), name.data());
  }

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
