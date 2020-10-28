// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <cpuid.h>
#include <lib/arch/intrin.h>
#include <stddef.h>
#include <stdint.h>

static inline bool jent_have_clock(void) {
  // Test for Invariant TSC (Fn8000_0007_EDX[8])
  uint32_t eax, ebx, ecx, edx;
  __cpuid_count(0x80000007, 0, eax, ebx, ecx, edx);
  return edx & (1 << 8);
}

static inline void jent_get_nstime(uint64_t* out) {
  // When running during boot, in particular before the VMM is up, our timers
  // haven't been calibrated yet. But, we only ever get here if
  // jent_have_clock returned true, so our system at least has an invariant
  // tsc. We could do some arithmetic to convert TSC -> nanoseconds, but raw
  // TSC is perfectly reasonable to use too (jitterentropy doesn't care about
  // the unit of time, just that the clock source is monotonic, invariant, and
  // high resolution).
  *out = _rdtsc();
}

static inline void* jent_zalloc(size_t len) { return NULL; }

static inline void jent_zfree(void* ptr, size_t len) {}

static inline int jent_fips_enabled(void) { return 0; }

static inline uint64_t rol64(uint64_t x, uint32_t n) { return (x << n) | (x >> (64 - n)); }
