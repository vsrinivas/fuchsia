//===-- secondary.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_SECONDARY_H_
#define SCUDO_SECONDARY_H_

#include "common.h"
#include "mutex.h"
#include "stats.h"
#include "string_utils.h"

namespace scudo {

namespace LargeBlock {

struct Header {
  LargeBlock::Header *Prev;
  LargeBlock::Header *Next;
  uptr BlockEnd;
  uptr PlatformData[4];
};

constexpr uptr getHeaderSize() {
  return roundUpTo(sizeof(Header), 1U << SCUDO_MIN_ALIGNMENT_LOG);
}

static Header *getHeader(uptr Ptr) {
  return reinterpret_cast<Header *>(Ptr - getHeaderSize());
}

static Header *getHeader(const void *Ptr) {
  return getHeader(reinterpret_cast<uptr>(Ptr));
}

} // namespace LargeBlock

class LargeMmapAllocator {
public:
  void initLinkerInitialized(GlobalStats *S) {
    Stats.initLinkerInitialized();
    if (S)
      S->link(&Stats);
  }
  void init(GlobalStats *S) {
    memset(this, 0, sizeof(*this));
    initLinkerInitialized(S);
  }

  // The alignment parameter serves a hint to be able unmap spurious memory when
  // dealing with larger alignments.
  void *allocate(uptr Size, uptr AlignmentHint = 0, uptr *BlockEnd = nullptr) {
    const uptr PageSize = getPageSizeCached();
    const uptr MapSize =
        roundUpTo(Size + LargeBlock::getHeaderSize(), PageSize) + 2 * PageSize;
    uptr PlatformData[4] = {};
    uptr MapBase = reinterpret_cast<uptr>(
        map(nullptr, MapSize, "scudo:secondary", MAP_NOACCESS | MAP_ALLOWNOMEM,
            PlatformData));
    if (UNLIKELY(!MapBase))
      return nullptr;
    uptr CommitBase = MapBase + PageSize;
    uptr MapEnd = MapBase + MapSize;

    // In the unlikely event of alignments larger than a page, adjust the amount
    // of memory we want to commit, and trim the extra memory.
    if (UNLIKELY(AlignmentHint >= PageSize)) {
      CommitBase = roundUpTo(MapBase + PageSize + 1, AlignmentHint) - PageSize;
      const uptr NewMapBase = CommitBase - PageSize;
      DCHECK_GE(NewMapBase, MapBase);
      // We only trim the extra memory on 32-bit platforms.
      if (SCUDO_WORDSIZE == 32U && NewMapBase != MapBase) {
        unmap((void *)MapBase, NewMapBase - MapBase, 0, PlatformData);
        MapBase = NewMapBase;
      }
      const uptr NewMapEnd =
          roundUpTo(MapBase + 2 * PageSize + Size, PageSize) + PageSize;
      DCHECK_LE(NewMapEnd, MapEnd);
      if (SCUDO_WORDSIZE == 32U && NewMapEnd != MapEnd) {
        unmap((void *)NewMapEnd, MapEnd - NewMapEnd, 0, PlatformData);
        MapEnd = NewMapEnd;
      }
    }

    const uptr CommitSize = MapEnd - PageSize - CommitBase;
    const uptr Ptr = reinterpret_cast<uptr>(
        map(reinterpret_cast<void *>(CommitBase), CommitSize, "scudo:secondary",
            0, PlatformData));
    LargeBlock::Header *H = reinterpret_cast<LargeBlock::Header *>(Ptr);
    H->BlockEnd = CommitBase + CommitSize;
    memcpy(H->PlatformData, PlatformData, sizeof(PlatformData));
    {
      SpinMutexLock L(&Mutex);
      if (!Tail) {
        Tail = H;
      } else {
        Tail->Next = H;
        H->Prev = Tail;
        Tail = H;
      }
      AllocatedBytes += CommitSize;
      if (LargestSize < CommitSize)
        LargestSize = CommitSize;
      NumberOfAllocs++;
      Stats.add(StatAllocated, CommitSize);
      Stats.add(StatMapped, CommitSize);
    }
    if (BlockEnd)
      *BlockEnd = CommitBase + CommitSize;
    return reinterpret_cast<void *>(Ptr + LargeBlock::getHeaderSize());
  }

  void deallocate(void *Ptr) {
    uptr PlatformData[4];
    LargeBlock::Header *H = LargeBlock::getHeader(Ptr);
    memcpy(PlatformData, H->PlatformData, sizeof(PlatformData));
    const uptr CommitSize = H->BlockEnd - reinterpret_cast<uptr>(H);
    {
      SpinMutexLock L(&Mutex);
      LargeBlock::Header *Prev = H->Prev;
      LargeBlock::Header *Next = H->Next;
      if (Prev) {
        CHECK_EQ(Prev->Next, H);
        Prev->Next = Next;
      }
      if (Next) {
        CHECK_EQ(Next->Prev, H);
        Next->Prev = Prev;
      }
      if (Tail == H) {
        CHECK(!Next);
        Tail = Prev;
      } else {
        CHECK(Next);
      }
      FreedBytes += CommitSize;
      NumberOfFrees++;
      Stats.sub(StatAllocated, CommitSize);
      Stats.sub(StatMapped, CommitSize);
    }
    unmap(reinterpret_cast<void *>(H), CommitSize, UNMAP_ALL, PlatformData);
  }

  static uptr getBlockEnd(void *Ptr) {
    return LargeBlock::getHeader(Ptr)->BlockEnd;
  }

  static uptr getBlockSize(void *Ptr) {
    return getBlockEnd(Ptr) - reinterpret_cast<uptr>(Ptr);
  }

  void printStats() const {
    Printf("Stats: LargeMmapAllocator: allocated %zd times (%zdK), "
           "freed %zd times (%zdK), remains %zd (%zdK) max %zdM\n",
           NumberOfAllocs, AllocatedBytes >> 10, NumberOfFrees,
           FreedBytes >> 10, NumberOfAllocs - NumberOfFrees,
           (AllocatedBytes - FreedBytes) >> 10, LargestSize >> 20);
  }

  void disable() { Mutex.lock(); }

  void enable() { Mutex.unlock(); }

  template <typename F> void iterateOverBlocks(F Callback) const {
    for (LargeBlock::Header *H = Tail; H != nullptr; H = H->Prev)
      Callback(reinterpret_cast<uptr>(H) + LargeBlock::getHeaderSize());
  }

private:
  StaticSpinMutex Mutex;
  u32 NumberOfAllocs;
  u32 NumberOfFrees;
  uptr AllocatedBytes;
  uptr FreedBytes;
  uptr LargestSize;
  LargeBlock::Header *Tail;
  LocalStats Stats;
};

} // namespace scudo

#endif // SCUDO_SECONDARY_H_
