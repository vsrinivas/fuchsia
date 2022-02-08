// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/x86/boot-cpuid.h>
#include <lib/special-sections/special-sections.h>

namespace arch {
namespace internal {

// The zeroth basic, hypervisor, and extended leaves (0, 0x4000'0000, and
// 0x8000'0000) are handled specially in InitializeBootCpuid itself.
// Note that they are not in the special section.
CpuidIo gBootCpuid0;
CpuidIo gBootCpuidHyp0;
CpuidIo gBootCpuidExt0;

// These leaves are used from assembly code that needs unmangled names for
// them, but InitializeBootCpuid handles them like implicit instantiations.

SPECIAL_SECTION("BootCpuidData", uint32_t) CpuidIo gBootCpuidFeature;
SPECIAL_SECTION("BootCpuidLeaf", uint32_t)
static const uint32_t kBootCpuidFeatureLeaf[2] = {CpuidFeatureFlagsC::kLeaf, 0};

SPECIAL_SECTION("BootCpuidData", uint32_t) CpuidIo gBootCpuidExtf;
SPECIAL_SECTION("BootCpuidLeaf", uint32_t)
static const uint32_t kBootCpuidExtfLeaf[2] = {CpuidExtendedFeatureFlagsB::kLeaf, 0};

}  // namespace internal
}  // namespace arch
