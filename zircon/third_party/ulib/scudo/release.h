//===-- release.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_RELEASE_H_
#define SCUDO_RELEASE_H_

#include "common.h"
#include "list.h"

namespace scudo {

class MemoryMapper {
public:
  MemoryMapper(uptr Addr = 0, uptr *Extra = nullptr)
      : ReleasedRangesCount(0), ReleasedBytes(0), BaseAddress(Addr),
        PlatformData(Extra) {}

  uptr getReleasedRangesCount() const { return ReleasedRangesCount; }

  uptr getReleasedBytes() const { return ReleasedBytes; }

  // Releases [From, To) range of pages back to OS.
  void releasePageRangeToOS(uptr From, uptr To) {
    const uptr Offset = From - BaseAddress;
    const uptr Size = To - From;
    releasePagesToOS(BaseAddress, Offset, Size, PlatformData);
    ReleasedRangesCount++;
    ReleasedBytes += Size;
  }

private:
  uptr ReleasedRangesCount;
  uptr ReleasedBytes;
  uptr BaseAddress;
  uptr *PlatformData;
};

// A packed array of Counters. Each counter occupies 2^N bits, enough to store
// counter's MaxValue. Ctor will try to allocate the required Buffer via map()
// and the caller is expected to check whether the initialization was successful
// by checking isAllocated() result. For the performance sake, none of the
// accessors check the validity of the arguments, It is assumed that Index is
// always in [0, N) range and the value is not incremented past MaxValue.
class PackedCounterArray {
public:
  PackedCounterArray(uptr NumCounters, uptr MaxValue) : N(NumCounters) {
    CHECK_GT(NumCounters, 0);
    CHECK_GT(MaxValue, 0);
    constexpr uptr MaxCounterBits = sizeof(*Buffer) * 8UL;
    // Rounding counter storage size up to the power of two allows for using
    // bit shifts calculating particular counter's Index and offset.
    const uptr CounterSizeBits =
        roundUpToPowerOfTwo(getMostSignificantSetBitIndex(MaxValue) + 1);
    CHECK_LE(CounterSizeBits, MaxCounterBits);
    CounterSizeBitsLog = getLog2(CounterSizeBits);
    CounterMask = ~0ULL >> (MaxCounterBits - CounterSizeBits);

    const uptr PackingRatio = MaxCounterBits >> CounterSizeBitsLog;
    CHECK_GT(PackingRatio, 0);
    PackingRatioLog = getLog2(PackingRatio);
    BitOffsetMask = PackingRatio - 1;

    BufferSize = (roundUpTo(N, 1ULL << PackingRatioLog) >> PackingRatioLog) *
                 sizeof(*Buffer);
    Buffer = reinterpret_cast<u64 *>(
        map(nullptr, BufferSize, "scudo:counters", MAP_ALLOWNOMEM));
  }
  ~PackedCounterArray() {
    if (isAllocated())
      unmap(reinterpret_cast<void *>(Buffer), BufferSize);
  }

  bool isAllocated() const { return !!Buffer; }

  uptr getCount() const { return N; }

  uptr get(uptr I) const {
    DCHECK_LT(I, N);
    const uptr Index = I >> PackingRatioLog;
    const uptr BitOffset = (I & BitOffsetMask) << CounterSizeBitsLog;
    return (Buffer[Index] >> BitOffset) & CounterMask;
  }

  void inc(uptr I) const {
    DCHECK_LT(get(I), CounterMask);
    const uptr Index = I >> PackingRatioLog;
    const uptr BitOffset = (I & BitOffsetMask) << CounterSizeBitsLog;
    Buffer[Index] += 1ULL << BitOffset;
  }

  void incRange(uptr From, uptr To) const {
    DCHECK_LE(From, To);
    for (uptr I = From; I <= To; I++)
      inc(I);
  }

private:
  const uptr N;
  uptr CounterSizeBitsLog;
  u64 CounterMask;
  uptr PackingRatioLog;
  u64 BitOffsetMask;

  uptr BufferSize;
  u64 *Buffer;
};

class FreePagesRangeTracker {
public:
  explicit FreePagesRangeTracker(MemoryMapper *MM, uptr Base)
      : Mapper(MM), BaseAddress(Base),
        PageSizeLog(getLog2(getPageSizeCached())), InRange(false),
        CurrentPage(0), CurrentRangeStatePage(0) {}

  void processNextPage(bool Freed) {
    if (Freed) {
      if (!InRange) {
        CurrentRangeStatePage = CurrentPage;
        InRange = true;
      }
    } else {
      closeOpenedRange();
    }
    CurrentPage++;
  }

  void finish() { closeOpenedRange(); }

private:
  void closeOpenedRange() {
    if (InRange) {
      Mapper->releasePageRangeToOS(BaseAddress +
                                       (CurrentRangeStatePage << PageSizeLog),
                                   BaseAddress + (CurrentPage << PageSizeLog));
      InRange = false;
    }
  }

  MemoryMapper *const Mapper;
  const uptr BaseAddress;
  const uptr PageSizeLog;
  bool InRange;
  uptr CurrentPage;
  uptr CurrentRangeStatePage;
};

template <class TransferBatchT>
NOINLINE void
releaseFreeMemoryToOS(const IntrusiveList<TransferBatchT> *FreeList, uptr Base,
                      uptr AllocatedPagesCount, uptr ChunkSize,
                      MemoryMapper *MM) {
  const uptr PageSize = getPageSizeCached();

  // Figure out the number of chunks per page and whether we can take a fast
  // path (the number of chunks per page is the same for all pages).
  uptr FullPagesChunkCountMax;
  bool SameChunkCountPerPage;
  if (ChunkSize <= PageSize && PageSize % ChunkSize == 0) {
    // Same number of chunks per page, no cross overs.
    FullPagesChunkCountMax = PageSize / ChunkSize;
    SameChunkCountPerPage = true;
  } else if (ChunkSize <= PageSize && PageSize % ChunkSize != 0 &&
             ChunkSize % (PageSize % ChunkSize) == 0) {
    // Some chunks are crossing page boundaries, which means that the page
    // contains one or two partial chunks, but all pages contain the same
    // number of chunks.
    FullPagesChunkCountMax = PageSize / ChunkSize + 1;
    SameChunkCountPerPage = true;
  } else if (ChunkSize <= PageSize) {
    // Some chunks are crossing page boundaries, which means that the page
    // contains one or two partial chunks.
    FullPagesChunkCountMax = PageSize / ChunkSize + 2;
    SameChunkCountPerPage = false;
  } else if (ChunkSize > PageSize && ChunkSize % PageSize == 0) {
    // One chunk covers multiple pages, no cross overs.
    FullPagesChunkCountMax = 1;
    SameChunkCountPerPage = true;
  } else if (ChunkSize > PageSize) {
    // One chunk covers multiple pages, Some chunks are crossing page
    // boundaries. Some pages contain one chunk, some contain two.
    FullPagesChunkCountMax = 2;
    SameChunkCountPerPage = false;
  } else {
    UNREACHABLE("All ChunkSize/PageSize ratios must be handled.");
  }

  PackedCounterArray Counters(AllocatedPagesCount, FullPagesChunkCountMax);
  if (!Counters.isAllocated())
    return;

  const uptr PageSizeLog = getLog2(PageSize);
  const uptr End = Base + AllocatedPagesCount * PageSize;

  // Iterate over free chunks and count how many free chunks affect each
  // allocated page.
  if (ChunkSize <= PageSize && PageSize % ChunkSize == 0) {
    // Each chunk affects one page only.
    for (auto It = FreeList->begin(); It != FreeList->end(); ++It) {
      for (u32 I = 0; I < (*It).getCount(); I++) {
        const uptr P = (uptr)((*It).get(I));
        if (P >= Base && P < End)
          Counters.inc((P - Base) >> PageSizeLog);
      }
    }
  } else {
    // In all other cases chunks might affect more than one page.
    for (auto It = FreeList->begin(); It != FreeList->end(); ++It) {
      for (u32 I = 0; I < (*It).getCount(); I++) {
        const uptr P = (uptr)((*It).get(I));
        if (P >= Base && P < End)
          Counters.incRange((P - Base) >> PageSizeLog,
                            (P - Base + ChunkSize - 1) >> PageSizeLog);
      }
    }
  }

  // Iterate over pages detecting ranges of pages with chunk Counters equal
  // to the expected number of chunks for the particular page.
  FreePagesRangeTracker RangeTracker(MM, Base);
  if (SameChunkCountPerPage) {
    // Fast path, every page has the same number of chunks affecting it.
    for (uptr I = 0; I < Counters.getCount(); I++)
      RangeTracker.processNextPage(Counters.get(I) == FullPagesChunkCountMax);
  } else {
    // Show path, go through the pages keeping count how many chunks affect
    // each page.
    const uptr Pn = ChunkSize < PageSize ? PageSize / ChunkSize : 1;
    const uptr Pnc = Pn * ChunkSize;
    // The idea is to increment the current page pointer by the first chunk
    // size, middle portion size (the portion of the page covered by chunks
    // except the first and the last one) and then the last chunk size, adding
    // up the number of chunks on the current page and checking on every step
    // whether the page boundary was crossed.
    uptr PrevPageBoundary = 0;
    uptr CurrentBoundary = 0;
    for (uptr I = 0; I < Counters.getCount(); I++) {
      uptr PageBoundary = PrevPageBoundary + PageSize;
      uptr ChunksPerPage = Pn;
      if (CurrentBoundary < PageBoundary) {
        if (CurrentBoundary > PrevPageBoundary)
          ChunksPerPage++;
        CurrentBoundary += Pnc;
        if (CurrentBoundary < PageBoundary) {
          ChunksPerPage++;
          CurrentBoundary += ChunkSize;
        }
      }
      PrevPageBoundary = PageBoundary;

      RangeTracker.processNextPage(Counters.get(I) == ChunksPerPage);
    }
  }
  RangeTracker.finish();
}

} // namespace scudo

#endif // SCUDO_RELEASE_H_
