// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/x86/boot-cpuid.h>
#include <lib/special-sections/special-sections.h>

namespace arch::internal {

// The zeroth basic, hypervisor, and extended leaves (0, 0x4000'0000, and
// 0x8000'0000) are handled specially in InitializeBootCpuid itself.
// Note that they are not described in the special section.
CpuidIo gBootCpuid0;
CpuidIo gBootCpuidHyp0;
CpuidIo gBootCpuidExt0;

// These are defined with assembly-friendly names, but have standard metadata
// to get them initialized by InitializeBootCpuid only if they're linked in.
CpuidIo gBootCpuidFeature;
CpuidIo gBootCpuidExtf;

}  // namespace arch::internal
