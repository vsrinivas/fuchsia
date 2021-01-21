// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_ARM64_INCLUDE_LIB_ARCH_SELF_MODIFICATION_H_
#define ZIRCON_KERNEL_LIB_ARCH_ARM64_INCLUDE_LIB_ARCH_SELF_MODIFICATION_H_

namespace arch {

// Ensures that the instruction cache is appropriately invalidated after
// self-modification and that no fetched instructions are stale.
inline void PostSelfModificationCacheSync() {
  // Invalidate the entire instruction cache to the point of unification.
  __asm__ volatile("ic ialluis\nisb" ::: "memory");
}

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_ARM64_INCLUDE_LIB_ARCH_SELF_MODIFICATION_H_
