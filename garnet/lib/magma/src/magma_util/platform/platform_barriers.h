// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_BARRIERS_H_
#define GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_BARRIERS_H_

#include "hw/arch_ops.h"

namespace magma {
namespace barriers {

// On Aarch64 we're often going to be synchronizing with non-cache-coherent devices, so use the dsb
// variants. They also synchronize with cache flush operations. We use the full-system variations
// because some GPUs may not be in the outer-shareable domain.

// Ensures that all writes before this call happen before any writes after this call.
inline void WriteBarrier() {
#if defined(__aarch64__)
  asm volatile("dsb st" : : : "memory");
#else
  hw_wmb();
#endif
}

// Ensures that all reads before this call happen before any reads after this call.
inline void ReadBarrier() {
#if defined(__aarch64__)
  asm volatile("dsb ld" : : : "memory");
#else
  hw_rmb();
#endif
}

// Ensures that all reads and writes before this call happen before any reads or writes after this
// call.
inline void Barrier() {
#if defined(__aarch64__)
  asm volatile("dsb sy" : : : "memory");
#else
  hw_mb();
#endif
}

}  // namespace barriers
}  // namespace magma

#endif  // GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_BARRIERS_H_
