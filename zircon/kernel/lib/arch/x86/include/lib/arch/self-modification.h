// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_X86_INCLUDE_LIB_ARCH_SELF_MODIFICATION_H_
#define ZIRCON_KERNEL_LIB_ARCH_X86_INCLUDE_LIB_ARCH_SELF_MODIFICATION_H_

#include <cpuid.h>

#include <cstdint>

namespace arch {

// Ensures that the instruction cache is appropriately invalidated after
// self-modification and that no fetched instructions are stale.
inline void PostSelfModificationCacheSync() {
  // [amd/vol2]: 7.6.1  Cache Organization and Operation.
  // AMD documents that the processor will do its own checking and flushing of
  // the instruction cache, so that software need not take any action.
  //
  // [intel/vol3]: 8.1.3  Handling Self- and Cross-Modifying Code.
  // Intel recommends executing a serializing instruction after any self- or
  // cross-modification - and in particular gives CPUID as an example (which is
  // also a serializaing instruction for AMD).
  uint32_t x;
  __cpuid(0, x, x, x, x);
}

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_X86_INCLUDE_LIB_ARCH_SELF_MODIFICATION_H_
