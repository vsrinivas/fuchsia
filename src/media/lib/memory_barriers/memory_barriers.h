// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_MEDIA_LIB_MEMORY_BARRIERS_MEMORY_BARRIERS_H_
#define SRC_MEDIA_LIB_MEMORY_BARRIERS_MEMORY_BARRIERS_H_

// This barrier should be used after a cache flush of memory before a MMIO is
// made to access it from the hardware.
inline void BarrierAfterFlush() {
#if defined(__aarch64__)
  // According to the ARMv8 ARM K11.5.4 it's better to use DSB instead of DMB
  // for ordering with respect to MMIO (DMB is ok if all agents are just
  // observing memory). The system shareability domain is used because that's
  // the only domain the video decoder is guaranteed to be in. SY is used
  // instead of LD or ST because section B2.3.5 says that the barrier needs both
  // read and write access types to be effective with regards to cache
  // operations.
  asm __volatile__("dsb sy");
#elif defined(__x86_64__)
  // This is here just in case we both (a) don't need to flush cache on x86 due to cache coherent
  // DMA (CLFLUSH not needed), and (b) we have code using non-temporal stores or "string
  // operations" whose surrounding code didn't itself take care of doing an SFENCE.  After returning
  // from this function, we may write to MMIO to start DMA - we want any previous (program order)
  // non-temporal stores to be visible to HW before that MMIO write that starts DMA.  The MFENCE
  // instead of SFENCE is mainly paranoia, though one could hypothetically create HW that starts or
  // continues DMA based on an MMIO read (please don't), in which case MFENCE might be needed here
  // before that read.
  asm __volatile__("mfence");
#else
#error need definition for this platform
#endif
}

// This barrier should be used after the hardware has signaled that memory has
// data but before the cache invalidate. See ARMv8 ARM K11.5.1 for the reason
// a barrier is necessary.
inline void BarrierBeforeInvalidate() {
#if defined(__aarch64__)
  // According to the ARMv8 ARM K11.5.4 it's better to use DSB instead of DMB
  // for ordering with respect to MMIO (DMB is ok if all agents are just
  // observing memory). The system shareability domain is used because that's
  // the only domain the video decoder is guaranteed to be in. SY is used
  // instead of LD or ST because section B2.3.5 says that the barrier needs both
  // read and write access types to be effective with regards to cache
  // operations.
  asm __volatile__("dsb sy");
#elif defined(__x86_64__)
  // This mfence may not be necessary due to cache coherent DMA on x86.
  asm __volatile__("mfence");
#else
#error need definition for this platform
#endif
}

// This barrier should be used after hardware has signaled it's done with a
// buffer but before releasing it. It's probably often unnecessary to use this
// barrier because there is another implicit dependency relationship.
inline void BarrierBeforeRelease() {
#if defined(__aarch64__)
  // According to the ARMv8 ARM K11.5.4 it's better to use DSB instead of DMB
  // for ordering with respect to MMIO (DMB is ok if all agents are just
  // observing memory). The system shareability domain is used because that's
  // the only domain the video decoder is guaranteed to be in. SY is used
  // instead of ST because we're not sure about the next operation on the
  // buffer, and LD isn't used because the caller may have determined that the
  // buffer can be released in several ways.
  asm __volatile__("dsb sy");
#elif defined(__x86_64__)
  // This mfence may not be necessary.
  asm __volatile__("mfence");
#else
#error need definition for this platform
#endif
}

#endif  // SRC_MEDIA_LIB_MEMORY_BARRIERS_MEMORY_BARRIERS_H_
