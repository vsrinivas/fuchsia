// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/concurrent/copy.h>
#include <lib/stdcompat/atomic.h>
#include <zircon/assert.h>

namespace concurrent {
namespace internal {

template <typename T, CopyDir kDirection, std::memory_order kOrder>
inline void CopyElement(void* _dst, const void* _src, size_t offset_bytes) {
  const T* src = reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(_src) + offset_bytes);
  T* dst = reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(_dst) + offset_bytes);

  static_assert(((kDirection == CopyDir::To) && ((kOrder == std::memory_order_relaxed) ||
                                                 (kOrder == std::memory_order_release))) ||
                ((kDirection == CopyDir::From) &&
                 ((kOrder == std::memory_order_relaxed) || (kOrder == std::memory_order_acquire))));

  if constexpr (kDirection == CopyDir::To) {
    cpp20::atomic_ref(*dst).store(*src, kOrder);
  } else {
    *dst = cpp20::atomic_ref(*src).load(kOrder);
  }
}

template <CopyDir kDir, SyncOpt kSyncOpt, MaxTransferAligned kMaxTransferAligned>
void WellDefinedCopy(void* dst, const void* src, size_t size_bytes) {
  // To keep life simple, we demand that both the source and the destination
  // have the same alignment relative to our max transfer granularity.
  ZX_DEBUG_ASSERT((reinterpret_cast<uintptr_t>(src) & (kMaxTransferGranularity - 1)) ==
                  (reinterpret_cast<uintptr_t>(dst) & (kMaxTransferGranularity - 1)));

  // In debug builds, make sure that src and dst obey the template specified
  // worst case alignment.
  //
  // TODO(johngro): Consider demoting these asserts to existing only in a
  // super-duper-debug build, if we ever have such a thing.
  ZX_DEBUG_ASSERT((kMaxTransferAligned == MaxTransferAligned::No) ||
                  (reinterpret_cast<uintptr_t>(src) & (kMaxTransferGranularity - 1)) == 0);
  ZX_DEBUG_ASSERT((kMaxTransferAligned == MaxTransferAligned::No) ||
                  (reinterpret_cast<uintptr_t>(dst) & (kMaxTransferGranularity - 1)) == 0);

  // Sync options at this point should be either to use Acquire/Release on the
  // options, or to simply use relaxed.  Use of fences should have been handled
  // at the inline wrapper level.
  static_assert((kSyncOpt == SyncOpt::AcqRelOps) || (kSyncOpt == SyncOpt::None));

  if (size_bytes == 0) {
    return;
  }

  constexpr std::memory_order kOrder =
      (kSyncOpt == SyncOpt::None)
          ? std::memory_order_relaxed
          : ((kDir == CopyDir::To) ? std::memory_order_release : std::memory_order_acquire);

  // Start by bringing our pointer to 8 byte alignment.  Skip any steps which
  // are not required based on our template specified worst case alignment.
  size_t offset_bytes = 0;
  if constexpr (kMaxTransferAligned == MaxTransferAligned::No) {
    const size_t current_alignment = (reinterpret_cast<uintptr_t>(src) & (sizeof(uint64_t) - 1));
    if (current_alignment > 0) {
      const size_t align_bytes = std::min(size_bytes, sizeof(uint64_t) - current_alignment);

      switch (align_bytes) {
        case 1:
          CopyElement<uint8_t, kDir, kOrder>(dst, src, 0);
          break;
        case 2:
          CopyElement<uint16_t, kDir, kOrder>(dst, src, 0);
          break;
        case 3:
          CopyElement<uint8_t, kDir, kOrder>(dst, src, 0);
          CopyElement<uint16_t, kDir, kOrder>(dst, src, 1);
          break;
        case 4:
          CopyElement<uint32_t, kDir, kOrder>(dst, src, 0);
          break;
        case 5:
          CopyElement<uint8_t, kDir, kOrder>(dst, src, 0);
          CopyElement<uint32_t, kDir, kOrder>(dst, src, 1);
          break;
        case 6:
          CopyElement<uint16_t, kDir, kOrder>(dst, src, 0);
          CopyElement<uint32_t, kDir, kOrder>(dst, src, 2);
          break;
        case 7:
          CopyElement<uint8_t, kDir, kOrder>(dst, src, 0);
          CopyElement<uint16_t, kDir, kOrder>(dst, src, 1);
          CopyElement<uint32_t, kDir, kOrder>(dst, src, 3);
          break;
        default:
          ZX_DEBUG_ASSERT(false);
      }

      offset_bytes += align_bytes;
    }
  }

  // Now copy the bulk portion of the data using 64 bit transfers.
  static_assert(kMaxTransferGranularity == sizeof(uint64_t));
  while (offset_bytes + sizeof(uint64_t) <= size_bytes) {
    CopyElement<uint64_t, kDir, kOrder>(dst, src, offset_bytes);
    offset_bytes += sizeof(uint64_t);
  }

  // If there is anything left to do, take care of the remainder using smaller
  // transfers.
  size_t remainder = size_bytes - offset_bytes;
  if (remainder > 0) {
    switch (remainder) {
      case 1:
        CopyElement<uint8_t, kDir, kOrder>(dst, src, offset_bytes + 0);
        break;
      case 2:
        CopyElement<uint16_t, kDir, kOrder>(dst, src, offset_bytes + 0);
        break;
      case 3:
        CopyElement<uint16_t, kDir, kOrder>(dst, src, offset_bytes + 0);
        CopyElement<uint8_t, kDir, kOrder>(dst, src, offset_bytes + 2);
        break;
      case 4:
        CopyElement<uint32_t, kDir, kOrder>(dst, src, offset_bytes + 0);
        break;
      case 5:
        CopyElement<uint32_t, kDir, kOrder>(dst, src, offset_bytes + 0);
        CopyElement<uint8_t, kDir, kOrder>(dst, src, offset_bytes + 4);
        break;
      case 6:
        CopyElement<uint32_t, kDir, kOrder>(dst, src, offset_bytes + 0);
        CopyElement<uint16_t, kDir, kOrder>(dst, src, offset_bytes + 4);
        break;
      case 7:
        CopyElement<uint32_t, kDir, kOrder>(dst, src, offset_bytes + 0);
        CopyElement<uint16_t, kDir, kOrder>(dst, src, offset_bytes + 4);
        CopyElement<uint8_t, kDir, kOrder>(dst, src, offset_bytes + 6);
        break;
      default:
        ZX_DEBUG_ASSERT(false);
    }
  }
}

// explicit instantiation of all of the different forms of WellDefinedCopy
using SO = SyncOpt;
using MTA = MaxTransferAligned;

template void WellDefinedCopy<CopyDir::To, SO::AcqRelOps, MTA::No>(void*, const void*, size_t);
template void WellDefinedCopy<CopyDir::To, SO::AcqRelOps, MTA::Yes>(void*, const void*, size_t);
template void WellDefinedCopy<CopyDir::To, SO::None, MTA::No>(void*, const void*, size_t);
template void WellDefinedCopy<CopyDir::To, SO::None, MTA::Yes>(void*, const void*, size_t);

template void WellDefinedCopy<CopyDir::From, SO::AcqRelOps, MTA::No>(void*, const void*, size_t);
template void WellDefinedCopy<CopyDir::From, SO::AcqRelOps, MTA::Yes>(void*, const void*, size_t);
template void WellDefinedCopy<CopyDir::From, SO::None, MTA::No>(void*, const void*, size_t);
template void WellDefinedCopy<CopyDir::From, SO::None, MTA::Yes>(void*, const void*, size_t);

}  // namespace internal
}  // namespace concurrent
