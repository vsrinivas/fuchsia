// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_HOST_INCLUDE_LIB_ARCH_INTRIN_H_
#define ZIRCON_KERNEL_LIB_ARCH_HOST_INCLUDE_LIB_ARCH_INTRIN_H_

#ifdef __cplusplus

#include <cstdint>

// Provide the machine-independent <lib/arch/intrin.h> API.  This file defines
// dummy versions that are sufficient to compile code using the generic API
// in host contexts, e.g. for unit tests and generator programs.

namespace arch {

/// Yield the processor momentarily.  This should be used in busy waits.
inline void Yield() {}

/// Synchronize all memory accesses of all kinds.
inline void DeviceMemoryBarrier() {}

/// Synchronize the ordering of all memory accesses wrt other CPUs.
inline void ThreadMemoryBarrier() {}

/// Return the current CPU cycle count.
inline uint64_t Cycles() { return 0; }

}  // namespace arch

#endif  // __cplusplus

#endif  // ZIRCON_KERNEL_LIB_ARCH_HOST_INCLUDE_LIB_ARCH_INTRIN_H_
