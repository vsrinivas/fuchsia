// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_X86_INCLUDE_LIB_ARCH_INTRIN_H_
#define ZIRCON_KERNEL_LIB_ARCH_X86_INCLUDE_LIB_ARCH_INTRIN_H_

#ifndef __ASSEMBLER__
#include <stddef.h>
#include <stdint.h>

// Provide the standard Intel x86 intrinsics API via the compiler headers.

#if !defined(__clang__)
// GCC's <x86intrin.h> indirectly includes its <mm_malloc.h>, which
// is useless to us but requires an <errno.h> with declarations.
#define _MM_MALLOC_H_INCLUDED
#endif

#include <emmintrin.h>
#include <x86intrin.h>

#ifdef __cplusplus

// Provide the machine-independent <lib/arch/intrin.h> API.

namespace arch {

/// Yield the processor momentarily.  This should be used in busy waits.
inline void Yield() { _mm_pause(); }

// TODO(fxbug.dev/49941): Improve the docs on the barrier APIs, maybe rename/refine.

/// Synchronize all memory accesses of all kinds.
inline void DeviceMemoryBarrier() { __asm__ volatile("mfence" ::: "memory"); }

/// Synchronize the ordering of all memory accesses wrt other CPUs.
inline void ThreadMemoryBarrier() { DeviceMemoryBarrier(); }

/// Return the current CPU cycle count.
inline uint64_t Cycles() { return _rdtsc(); }

}  // namespace arch

#endif  // __cplusplus

#endif  // !__ASSEMBLER__

#endif  // ZIRCON_KERNEL_LIB_ARCH_X86_INCLUDE_LIB_ARCH_INTRIN_H_
