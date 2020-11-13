// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/x86/boot-cpuid.h>

namespace arch {
namespace internal {

// The zeroth basic, hypervisor, and extended leaves (0, 0x4000'0000, and
// 0x8000'0000) are handled specially in InitializeBootCpuid itself.
// Note that they are not in the special section.
CpuidIo gBootCpuid0 = kBootCpuidInitializer<CpuidMaximumLeaf::kLeaf>;
CpuidIo gBootCpuidHyp0 = kBootCpuidInitializer<CpuidMaximumHypervisorLeaf::kLeaf>;
CpuidIo gBootCpuidExt0 = kBootCpuidInitializer<CpuidMaximumExtendedLeaf::kLeaf>;

// These leaves are used from assembly code that needs unmangled names for
// them, but InitializeBootCpuid handles them like implicit instantiations.

[[gnu::section("BootCpuid")]] alignas(uint32_t) CpuidIo gBootCpuidFeature =
    kBootCpuidInitializer<CpuidFeatureFlagsC::kLeaf>;

[[gnu::section("BootCpuid")]] alignas(uint32_t) CpuidIo gBootCpuidExtf =
    kBootCpuidInitializer<CpuidExtendedFeatureFlagsB::kLeaf>;

}  // namespace internal
}  // namespace arch
