// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_CONCURRENT_COPY_H_
#define LIB_CONCURRENT_COPY_H_

#include <lib/concurrent/common.h>
#include <lib/stdcompat/bit.h>
#include <stdint.h>

namespace concurrent {
namespace internal {

template <CopyDir, SyncOpt, MaxTransferAligned>
void WellDefinedCopy(void* dst, const void* src, size_t size_bytes);

}  // namespace internal

// Copy |size_bytes| bytes from |src| to |dst| using atomic store operations to move
// the element into |dst| so that the behavior of the system is always well
// defined, even if there is a WellDefinedCopyFrom operation reading from the
// memory pointed to by |dst| concurrent with this CopyTo operation.
//
// WellDefinedCopyTo has memcpy semantics, not memmove semantics. In other
// words, it is illegal for |src| or |dst| to overlap in any way.
//
// While it is not required, by default, that |src| and |dst| have any specific
// alignment, both |src| and |dst| *must* have the _same_ alignment.
//
// IOW: (|src| & 0x7) *must* equal (|dst| & 0x7)
//
// :: Template Args ::
//
// |kSyncOpt|
// Controls the options for memory order synchronization.  See the comments in
// lib/concurrent/common.h for details.
//
// |kWorstCaseAlignment|
// An explicit guarantee of the worst case alignment that |src|/|dst| will obey.
// When this alignment guarantee is greater than or equal to the maximum
// internal transfer granularity of 64 bits, the initial explicit alignment step
// of the operation can be optimized away for a minor performance gain.
template <SyncOpt kSyncOpt = SyncOpt::AcqRelOps, size_t kWorstCaseAlignment = 1>
void WellDefinedCopyTo(void* dst, const void* src, size_t size_bytes) {
  static_assert(cpp20::has_single_bit(kWorstCaseAlignment),
                "kWorstCaseAlignment must be a power of 2");
  constexpr internal::MaxTransferAligned kMTA =
      (kWorstCaseAlignment >= internal::kMaxTransferGranularity) ? internal::MaxTransferAligned::Yes
                                                                 : internal::MaxTransferAligned::No;

  if constexpr (kSyncOpt == SyncOpt::Fence) {
    std::atomic_thread_fence(std::memory_order_release);
    internal::WellDefinedCopy<internal::CopyDir::To, SyncOpt::None, kMTA>(dst, src, size_bytes);
  } else {
    internal::WellDefinedCopy<internal::CopyDir::To, kSyncOpt, kMTA>(dst, src, size_bytes);
  }
}

// Copy |size_bytes| bytes from |src| to |dst| using atomic load operations to load the
// element from |src| so that the behavior of the system is always well defined,
// even if there is a WellDefinedCopyTo operation writing to the memory pointed
// to by |src| concurrent with this CopyFrom operation.
//
// WellDefinedCopyFrom has memcpy semantics, not memmove semantics. In other
// words, it is illegal for |src| or |dst| to overlap in any way.
//
// While it is not required, by default, that |src| and |dst| have any specific
// alignment, both |src| and |dst| *must* have the _same_ alignment.
//
// IOW: (|src| & 0x7) *must* equal (|dst| & 0x7)
//
// :: Template Args ::
//
// |kSyncOpt|
// Controls the options for memory order synchronization.  See the comments in
// lib/concurrent/common.h for details.
//
// |kWorstCaseAlignment|
// An explicit guarantee of the worst case alignment that |src|/|dst| will obey.
// When this alignment guarantee is greater than or equal to the maximum
// internal transfer granularity of 64 bits, the initial explicit alignment step
// of the operation can be optimized away for a minor performance gain.
template <SyncOpt kSyncOpt = SyncOpt::AcqRelOps, size_t kWorstCaseAlignment = 1>
void WellDefinedCopyFrom(void* dst, const void* src, size_t size_bytes) {
  static_assert(cpp20::has_single_bit(kWorstCaseAlignment),
                "kWorstCaseAlignment must be a power of 2");
  constexpr internal::MaxTransferAligned kMTA =
      (kWorstCaseAlignment >= internal::kMaxTransferGranularity) ? internal::MaxTransferAligned::Yes
                                                                 : internal::MaxTransferAligned::No;

  if constexpr (kSyncOpt == SyncOpt::Fence) {
    internal::WellDefinedCopy<internal::CopyDir::From, SyncOpt::None, kMTA>(dst, src, size_bytes);
    std::atomic_thread_fence(std::memory_order_acquire);
  } else {
    internal::WellDefinedCopy<internal::CopyDir::From, kSyncOpt, kMTA>(dst, src, size_bytes);
  }
}

}  // namespace concurrent

#endif  // LIB_CONCURRENT_COPY_H_
