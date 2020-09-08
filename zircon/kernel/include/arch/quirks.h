// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_ARCH_QUIRKS_H_
#define ZIRCON_KERNEL_INCLUDE_ARCH_QUIRKS_H_

#include <ktl/type_traits.h>

// A small collection of functions used to deal with architecture specific
// quirks in sections of code which are generally architecture independent.

// If we have any A73 cores in the system, we need to work around
// Cortex-A73 erratum 858921, described in:
//
// https://static.docs.arm.com/epm086451/120/Cortex-A73_MPCore_Software_Developers_Errata_Notice.pdf
//
// Right now, this means that we need a special version of current_ticks() in
// the kernel, and to inject a special version of zx_ticks_get in the VDSO if
// our clients are not going to make a syscall in order to read the tick
// counter.
#if __aarch64__
bool arch_quirks_needs_arm_erratum_858921_mitigation();
#else
template <typename T = void>
static inline bool arch_quirks_needs_arm_erratum_858921_mitigation() {
  static_assert(!ktl::is_same_v<T, T>,
                "Do not call arch_quirks_needs_arm_erratum_858921_mitigation when building "
                "for non-ARM architectures");
  return false;
}
#endif

#endif  // ZIRCON_KERNEL_INCLUDE_ARCH_QUIRKS_H_
