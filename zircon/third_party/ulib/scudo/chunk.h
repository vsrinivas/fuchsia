//===-- chunk.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_CHUNK_H_
#define SCUDO_CHUNK_H_

#include "platform.h"

#include "atomic_helpers.h"
#include "checksum.h"
#include "common.h"
#include "report.h"

namespace scudo {

enum AllocType : u8 {
  FromMalloc = 0,
  FromNew = 1,
  FromNewArray = 2,
  FromMemalign = 3,
};

enum ChunkState : u8 {
  ChunkAvailable = 0,
  ChunkAllocated = 1,
  ChunkQuarantine = 2
};

typedef u64 PackedHeader;
// Update the 'Mask' constants to reflect changes in this structure.
struct UnpackedHeader {
  u64 Checksum : 16;
  u64 ClassId : 8;
  u64 SizeOrUnusedBytes : 20;
  u64 State : 2;
  u64 AllocType : 2;
  u64 Offset : 16;
};
typedef atomic_u64 AtomicPackedHeader;
COMPILER_CHECK(sizeof(UnpackedHeader) == sizeof(PackedHeader));

// Those constants are required to silence some -Werror=conversion errors when
// assigning values to the related bitfield variables.
constexpr uptr ChecksumMask = (1U << 16) - 1;
constexpr uptr ClassIdMask = (1U << 8) - 1;
constexpr uptr SizeOrUnusedBytesMask = (1U << 20) - 1;
// constexpr uptr StateMask = (1U << 2) - 1;  // Currently unused.
constexpr uptr AllocTypeMask = (1U << 2) - 1;
constexpr uptr OffsetMask = (1U << 16) - 1;

extern atomic_u8 HashAlgorithm;

INLINE u16 computeChecksum(u32 Seed, uptr Value, uptr *Array, uptr ArraySize) {
  // If the hardware CRC32 feature is defined here, it was enabled everywhere,
  // as opposed to only for crc32_hw.cc. This means that other hardware
  // specific instructions were likely emitted at other places, and as a
  // result there is no reason to not use it here.
#if defined(__SSE4_2__) || defined(__ARM_FEATURE_CRC32)
  u32 Crc = static_cast<u32>(CRC32_INTRINSIC(Seed, Value));
  for (uptr I = 0; I < ArraySize; I++)
    Crc = static_cast<u32>(CRC32_INTRINSIC(Crc, Array[I]));
  return static_cast<u16>((Crc & 0xffff) ^ (Crc >> 16));
#else
  if (atomic_load_relaxed(&HashAlgorithm) == HardwareCRC32) {
    u32 Crc = computeHardwareCRC32(Seed, Value);
    for (uptr I = 0; I < ArraySize; I++)
      Crc = computeHardwareCRC32(Crc, Array[I]);
    return static_cast<u16>((Crc & 0xffff) ^ (Crc >> 16));
  } else {
    u16 Checksum = computeBSDChecksum(static_cast<u16>(Seed & 0xffff), Value);
    for (uptr I = 0; I < ArraySize; I++)
      Checksum = computeBSDChecksum(Checksum, Array[I]);
    return Checksum;
  }
#endif // defined(__SSE4_2__) || defined(__ARM_FEATURE_CRC32)
}

namespace Chunk {

constexpr uptr getHeaderSize() {
  return roundUpTo(sizeof(PackedHeader), 1U << SCUDO_MIN_ALIGNMENT_LOG);
}

INLINE AtomicPackedHeader *getAtomicHeader(void *Ptr) {
  return reinterpret_cast<AtomicPackedHeader *>(reinterpret_cast<uptr>(Ptr) -
                                                getHeaderSize());
}

INLINE
const AtomicPackedHeader *getConstAtomicHeader(const void *Ptr) {
  return reinterpret_cast<const AtomicPackedHeader *>(
      reinterpret_cast<uptr>(Ptr) - getHeaderSize());
}

INLINE void *getBlockBegin(const void *Ptr, UnpackedHeader *Header) {
  return reinterpret_cast<void *>(reinterpret_cast<uptr>(Ptr) -
                                  getHeaderSize() -
                                  (Header->Offset << SCUDO_MIN_ALIGNMENT_LOG));
}

// We do not need a cryptographically strong hash for the checksum, but a CRC
// type function that can alert us in the event a header is invalid or
// corrupted. Ideally slightly better than a simple xor of all fields.
static INLINE u16 computeHeaderChecksum(u32 Cookie, const void *Ptr,
                                        UnpackedHeader *Header) {
  UnpackedHeader ZeroChecksumHeader = *Header;
  ZeroChecksumHeader.Checksum = 0;
  uptr HeaderHolder[sizeof(UnpackedHeader) / sizeof(uptr)];
  memcpy(&HeaderHolder, &ZeroChecksumHeader, sizeof(HeaderHolder));
  return computeChecksum(Cookie, reinterpret_cast<uptr>(Ptr), HeaderHolder,
                         ARRAY_SIZE(HeaderHolder));
}

INLINE void storeHeader(u32 Cookie, void *Ptr,
                        UnpackedHeader *NewUnpackedHeader) {
  NewUnpackedHeader->Checksum =
      computeHeaderChecksum(Cookie, Ptr, NewUnpackedHeader);
  PackedHeader NewPackedHeader = bit_cast<PackedHeader>(*NewUnpackedHeader);
  atomic_store_relaxed(getAtomicHeader(Ptr), NewPackedHeader);
}

INLINE
void loadHeader(u32 Cookie, const void *Ptr,
                UnpackedHeader *NewUnpackedHeader) {
  PackedHeader NewPackedHeader = atomic_load_relaxed(getConstAtomicHeader(Ptr));
  *NewUnpackedHeader = bit_cast<UnpackedHeader>(NewPackedHeader);
  if (UNLIKELY(NewUnpackedHeader->Checksum !=
               computeHeaderChecksum(Cookie, Ptr, NewUnpackedHeader)))
    reportHeaderCorruption(const_cast<void *>(Ptr));
}

INLINE void compareExchangeHeader(u32 Cookie, void *Ptr,
                                  UnpackedHeader *NewUnpackedHeader,
                                  UnpackedHeader *OldUnpackedHeader) {
  NewUnpackedHeader->Checksum =
      computeHeaderChecksum(Cookie, Ptr, NewUnpackedHeader);
  PackedHeader NewPackedHeader = bit_cast<PackedHeader>(*NewUnpackedHeader);
  PackedHeader OldPackedHeader = bit_cast<PackedHeader>(*OldUnpackedHeader);
  if (UNLIKELY(!atomic_compare_exchange_strong(
          getAtomicHeader(Ptr), &OldPackedHeader, NewPackedHeader,
          memory_order_relaxed)))
    reportHeaderRace(Ptr);
}

INLINE
bool isValid(u32 Cookie, const void *Ptr, UnpackedHeader *NewUnpackedHeader) {
  PackedHeader NewPackedHeader = atomic_load_relaxed(getConstAtomicHeader(Ptr));
  *NewUnpackedHeader = bit_cast<UnpackedHeader>(NewPackedHeader);
  return NewUnpackedHeader->Checksum ==
         computeHeaderChecksum(Cookie, Ptr, NewUnpackedHeader);
}

} // namespace Chunk

} // namespace scudo

#endif // SCUDO_CHUNK_H_
