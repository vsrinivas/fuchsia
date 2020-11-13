// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/x86/cpuid.h>

#include <hwreg/asm.h>

// Generate the <lib/arch/x86/cpuid-asm.h> header that assembly code uses.

int main(int argc, char** argv) {
  return hwreg::AsmHeader()
      .Macro("CPUID_EAX", arch::CpuidIo::kEax * sizeof(uint32_t))
      .Macro("CPUID_EBX", arch::CpuidIo::kEbx * sizeof(uint32_t))
      .Macro("CPUID_ECX", arch::CpuidIo::kEcx * sizeof(uint32_t))
      .Macro("CPUID_EDX", arch::CpuidIo::kEdx * sizeof(uint32_t))
      .Macro("CPUID_HYP_LEAF0", arch::CpuidMaximumHypervisorLeaf::kLeaf)
      .Macro("CPUID_EXT_LEAF0", arch::CpuidMaximumExtendedLeaf::kLeaf)
      .Register<arch::CpuidFeatureFlagsC>("CPUID_FEATURE_")
      .Register<arch::CpuidExtendedFeatureFlagsB>("CPUID_EXTF_")
      .Main(argc, argv);
}
