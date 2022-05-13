// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_HOST_INCLUDE_LIB_ARCH_TICKS_H_
#define ZIRCON_KERNEL_LIB_ARCH_HOST_INCLUDE_LIB_ARCH_TICKS_H_

#ifdef __cplusplus

#include <cstdint>

// Provide the machine-independent <lib/arch/ticks.h> API.  This file defines
// Stub versions that are sufficient to compile code using the generic API
// in host contexts, e.g. for unit tests and generator programs.

namespace arch {

struct EarlyTicks {
  int count;

  static EarlyTicks Get() { return {1}; }
  static constexpr EarlyTicks Zero() { return {0}; }
};

}  // namespace arch

#endif  // __cplusplus

#endif  // ZIRCON_KERNEL_LIB_ARCH_HOST_INCLUDE_LIB_ARCH_TICKS_H_
