// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/x86/boot-cpuid.h>

namespace arch {
namespace internal {

// Leaf 0 is handled specially in InitializeBootCpuid itself.
// Note that it is not in the special section.
CpuidIo gBootCpuid0 = kBootCpuidInitializer<CpuidMaximumLeaf>;

// These leaves are used from assembly code that needs unmangled names for
// them, but InitializeBootCpuid handles them like implicit instantiations.

[[gnu::section("BootCpuid")]] alignas(uint32_t) CpuidIo gBootCpuidFeature =
    kBootCpuidInitializer<CpuidFeatureFlagsC>;

[[gnu::section("BootCpuid")]] alignas(uint32_t) CpuidIo gBootCpuidExtf =
    kBootCpuidInitializer<CpuidExtendedFeatureFlagsB>;

}  // namespace internal
}  // namespace arch
