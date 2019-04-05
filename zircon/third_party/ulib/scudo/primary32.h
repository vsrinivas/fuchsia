//===-- primary32.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_PRIMARY32_H_
#define SCUDO_PRIMARY32_H_

#include "bytemap.h"
#include "common.h"
#include "list.h"
#include "local_cache.h"
#include "release.h"
#include "stats.h"
#include "string_utils.h"

namespace scudo {

template <class SizeClassMapT, uptr RegionSizeLog> class SizeClassAllocator32 {
public:
  typedef SizeClassMapT SizeClassMap;
  typedef SizeClassAllocator32<SizeClassMapT, RegionSizeLog> ThisT;
  typedef SizeClassAllocatorLocalCache<ThisT> CacheT;
  typedef typename CacheT::TransferBatch TransferBatch;

  static uptr getSizeByClassId(uptr ClassId) {
    return (ClassId == SizeClassMap::BatchClassId)
               ? sizeof(TransferBatch)
               : SizeClassMap::getSizeByClassId(ClassId);
  }

  static bool canAllocate(uptr Size) { return Size <= SizeClassMap::MaxSize; }

  void initLinkerInitialized(s32 ReleaseToOsInterval) {
    PossibleRegions.initLinkerInitialized();
    MinRegionIndex = NumRegions;
    // MaxRegionIndex = 0;  // Already initialized to 0.

    u32 Seed;
    if (UNLIKELY(!getRandom(reinterpret_cast<void *>(&Seed), sizeof(Seed))))
      Seed = (u32)((uptr)SizeClassInfoArray ^ getMonotonicTime());
    const uptr PageSize = getPageSizeCached();
    for (int I = 0; I < NumClasses; I++) {
      SizeClassInfo *Sci = getSizeClassInfo(I);
      Sci->RandState = getRandomU32(&Seed);
      // TODO(kostyak): make it configurable
      Sci->CanRelease = (ReleaseToOsInterval > 0) &&
                        (I != SizeClassMap::BatchClassId) &&
                        (getSizeByClassId(I) >= (PageSize / 32));
    }
    ReleaseToOsIntervalMs = ReleaseToOsInterval;
  }
  void init(s32 ReleaseToOsInterval) {
    memset(this, 0, sizeof(*this));
    initLinkerInitialized(ReleaseToOsInterval);
  }

  TransferBatch *popBatch(LocalStats *Stat, CacheT *C, uptr ClassId) {
    DCHECK_LT(ClassId, NumClasses);
    SizeClassInfo *Sci = getSizeClassInfo(ClassId);
    BlockingMutexLock L(&Sci->Mutex);
    TransferBatch *B = Sci->FreeList.front();
    if (B)
      Sci->FreeList.pop_front();
    else
      B = populateFreeList(Stat, C, Sci, ClassId);
    DCHECK_GT(B->getCount(), 0);
    Sci->Stats.PoppedBlocks += B->getCount();
    return B;
  }

  void pushBatch(uptr ClassId, TransferBatch *B) {
    DCHECK_LT(ClassId, NumClasses);
    DCHECK_GT(B->getCount(), 0);
    SizeClassInfo *Sci = getSizeClassInfo(ClassId);
    BlockingMutexLock L(&Sci->Mutex);
    Sci->FreeList.push_front(B);
    Sci->Stats.PushedBlocks += B->getCount();
    if (Sci->CanRelease)
      releaseToOSMaybe(Sci, ClassId);
  }

  void disable() {
    for (uptr I = 0; I < NumClasses; I++)
      getSizeClassInfo(I)->Mutex.lock();
  }

  void enable() {
    for (sptr I = (sptr)NumClasses - 1; I >= 0; I--)
      getSizeClassInfo(I)->Mutex.unlock();
  }

  template <typename F> void iterateOverBlocks(F Callback) {
    for (uptr I = MinRegionIndex; I < MaxRegionIndex; I++)
      if (PossibleRegions[I]) {
        const uptr BlockSize = getSizeByClassId(PossibleRegions[I]);
        const uptr From = I * RegionSize;
        const uptr To = From + (RegionSize / BlockSize) * BlockSize;
        for (uptr Block = From; Block < To; Block += BlockSize)
          Callback(Block);
      }
  }

  void printStats(uptr ClassId, uptr Rss) {
    SizeClassInfo *Sci = getSizeClassInfo(ClassId);
    if (Sci->AllocatedUser == 0)
      return;
    const uptr InUse = Sci->Stats.PoppedBlocks - Sci->Stats.PushedBlocks;
    const uptr AvailableChunks = Sci->AllocatedUser / getSizeByClassId(ClassId);
    Printf("  %02zd (%6zd): mapped: %6zdK allocs: %7zd frees: %7zd inuse: %6zd "
           "avail: %6zd rss: %6zdK\n",
           ClassId, getSizeByClassId(ClassId), Sci->AllocatedUser >> 10,
           Sci->Stats.PoppedBlocks, Sci->Stats.PushedBlocks, InUse,
           AvailableChunks, Rss >> 10);
  }

  void printStats() {
    // TODO(kostyak): get the RSS per region.
    uptr TotalMapped = 0;
    uptr PoppedBlocks = 0;
    uptr PushedBlocks = 0;
    for (uptr I = 0; I < NumClasses; I++) {
      SizeClassInfo *Sci = getSizeClassInfo(I);
      TotalMapped += Sci->AllocatedUser;
      PoppedBlocks += Sci->Stats.PoppedBlocks;
      PushedBlocks += Sci->Stats.PushedBlocks;
    }
    Printf("Stats: SizeClassAllocator32: %zdM mapped in %zd allocations; "
           "remains %zd\n",
           TotalMapped >> 20, PoppedBlocks, PoppedBlocks - PushedBlocks);
    for (uptr I = 0; I < NumClasses; I++)
      printStats(I, 0);
  }

  void releaseToOS() {
    for (uptr I = 1; I < NumClasses; I++) {
      SizeClassInfo *Sci = getSizeClassInfo(I);
      BlockingMutexLock L(&Sci->Mutex);
      releaseToOSMaybe(Sci, I, /*Force=*/true);
    }
  }

private:
  static const uptr NumClasses = SizeClassMap::NumClasses;
  static const uptr RegionSize = 1UL << RegionSizeLog;
  static const uptr NumRegions = SCUDO_MMAP_RANGE_SIZE >> RegionSizeLog;
#if SCUDO_WORDSIZE == 32U
  typedef FlatByteMap<NumRegions> ByteMap;
#else
  typedef TwoLevelByteMap<(NumRegions >> 12), 1U << 12> ByteMap;
#endif

  struct SizeClassStats {
    uptr PoppedBlocks;
    uptr PushedBlocks;
  };

  struct ReleaseToOsInfo {
    uptr PushedBlocksAtLastRelease;
    uptr RangesReleased;
    uptr LastReleasedBytes;
    u64 LastReleaseAtNs;
  };

  struct ALIGNED(SCUDO_CACHE_LINE_SIZE) SizeClassInfo {
    BlockingMutex Mutex;
    IntrusiveList<TransferBatch> FreeList;
    SizeClassStats Stats;
    u32 RandState;
    uptr AllocatedUser;
    bool CanRelease;
    ReleaseToOsInfo ReleaseInfo;
  };
  COMPILER_CHECK(sizeof(SizeClassInfo) % SCUDO_CACHE_LINE_SIZE == 0);

  uptr computeRegionId(uptr Mem) {
    const uptr Id = Mem >> RegionSizeLog;
    CHECK_LT(Id, NumRegions);
    return Id;
  }

  uptr allocateRegionSlow(LocalStats *Stat) {
    uptr MapSize = 2 * RegionSize;
    const uptr MapBase = reinterpret_cast<uptr>(
        map(nullptr, MapSize, "scudo:primary", MAP_ALLOWNOMEM));
    if (UNLIKELY(!MapBase))
      return 0;
    const uptr MapEnd = MapBase + MapSize;
    uptr Region = MapBase;
    if (isAligned(Region, RegionSize)) {
      SpinMutexLock L(&RegionsStashMutex);
      if (NumberOfStashedRegions < MaxStashedRegions)
        RegionsStash[NumberOfStashedRegions++] = MapBase + RegionSize;
      else
        MapSize = RegionSize;
    } else {
      Region = roundUpTo(MapBase, RegionSize);
      unmap((void *)MapBase, Region - MapBase);
      MapSize = RegionSize;
    }
    const uptr End = Region + MapSize;
    if (End != MapEnd)
      unmap(reinterpret_cast<void *>(End), MapEnd - End);
    Stat->add(StatMapped, MapSize);
    return Region;
  }

  uptr allocateRegion(LocalStats *Stat, uptr ClassId) {
    DCHECK_LT(ClassId, NumClasses);
    uptr Region = 0;
    {
      SpinMutexLock L(&RegionsStashMutex);
      if (NumberOfStashedRegions > 0)
        Region = RegionsStash[--NumberOfStashedRegions];
    }
    if (!Region)
      Region = allocateRegionSlow(Stat);
    if (LIKELY(Region)) {
      if (ClassId) {
        const uptr RegionIndex = computeRegionId(Region);
        if (RegionIndex < MinRegionIndex)
          MinRegionIndex = RegionIndex;
        if (RegionIndex > MaxRegionIndex)
          MaxRegionIndex = RegionIndex;
        PossibleRegions.set(RegionIndex, static_cast<u8>(ClassId));
      }
    }
    return Region;
  }

  SizeClassInfo *getSizeClassInfo(uptr ClassId) {
    DCHECK_LT(ClassId, NumClasses);
    return &SizeClassInfoArray[ClassId];
  }

  bool populateBatches(CacheT *C, SizeClassInfo *Sci, uptr ClassId,
                       TransferBatch **CurrentBatch, u32 MaxCount,
                       void **PointersArray, u32 Count) {
    if (ClassId != SizeClassMap::BatchClassId)
      shuffle(PointersArray, Count, &Sci->RandState);
    TransferBatch *B = *CurrentBatch;
    for (uptr I = 0; I < Count; I++) {
      if (B && B->getCount() == MaxCount) {
        Sci->FreeList.push_back(B);
        B = nullptr;
      }
      if (!B) {
        B = C->createBatch(ClassId, this, PointersArray[I]);
        if (UNLIKELY(!B))
          return false;
        B->clear();
      }
      B->add(PointersArray[I]);
    }
    *CurrentBatch = B;
    return true;
  }

  NOINLINE TransferBatch *populateFreeList(LocalStats *Stat, CacheT *C,
                                           SizeClassInfo *Sci, uptr ClassId) {
    const uptr Region = allocateRegion(Stat, ClassId);
    if (UNLIKELY(!Region))
      return nullptr;
    const uptr Size = getSizeByClassId(ClassId);
    const u32 MaxCount = TransferBatch::MaxCached(Size);
    DCHECK_GT(MaxCount, 0);
    const uptr NumberOfBlocks = RegionSize / Size;
    DCHECK_GT(NumberOfBlocks, 0);
    TransferBatch *B = nullptr;
    constexpr uptr ShuffleArraySize = 48;
    void *ShuffleArray[ShuffleArraySize];
    u32 Count = 0;
    const uptr AllocatedUser = NumberOfBlocks * Size;
    for (uptr I = Region; I < Region + AllocatedUser; I += Size) {
      ShuffleArray[Count++] = reinterpret_cast<void *>(I);
      if (Count == ShuffleArraySize) {
        if (UNLIKELY(!populateBatches(C, Sci, ClassId, &B, MaxCount,
                                      ShuffleArray, Count)))
          return nullptr;
        Count = 0;
      }
    }
    if (Count) {
      if (UNLIKELY(!populateBatches(C, Sci, ClassId, &B, MaxCount, ShuffleArray,
                                    Count)))
        return nullptr;
    }
    DCHECK(B);
    DCHECK_GT(B->getCount(), 0);
    Sci->AllocatedUser += AllocatedUser;
    if (Sci->CanRelease)
      Sci->ReleaseInfo.LastReleaseAtNs = getMonotonicTime();
    return B;
  }

  NOINLINE void releaseToOSMaybe(SizeClassInfo *Sci, uptr ClassId,
                                 bool Force = false) {
    const uptr BlockSize = getSizeByClassId(ClassId);
    const uptr PageSize = getPageSizeCached();

    CHECK_GE(Sci->Stats.PoppedBlocks, Sci->Stats.PushedBlocks);
    const uptr N = Sci->Stats.PoppedBlocks - Sci->Stats.PushedBlocks;
    if (N * BlockSize < PageSize)
      return; // No chance to release anything.
    if ((Sci->Stats.PushedBlocks - Sci->ReleaseInfo.PushedBlocksAtLastRelease) *
            BlockSize <
        PageSize) {
      return; // Nothing new to release.
    }

    if (!Force) {
      const s32 IntervalMs = ReleaseToOsIntervalMs;
      if (IntervalMs < 0)
        return;
      if (Sci->ReleaseInfo.LastReleaseAtNs + IntervalMs * 1000000ULL >
          getMonotonicTime()) {
        return; // Memory was returned recently.
      }
    }

    // TODO(kostyak): currently not ideal as we loop over all regions and
    // iterate multiple times over the same freelist if a ClassId spans
    // multiple regions. But it will have to do for now.
    MemoryMapper Mapper;
    for (uptr I = MinRegionIndex; I < MaxRegionIndex; I++) {
      if (PossibleRegions[I] == ClassId) {
        releaseFreeMemoryToOS(&Sci->FreeList, I * RegionSize,
                              RegionSize / PageSize, BlockSize, &Mapper);
      }
    }
    if (Mapper.getReleasedRangesCount() > 0) {
      Sci->ReleaseInfo.PushedBlocksAtLastRelease = Sci->Stats.PushedBlocks;
      Sci->ReleaseInfo.RangesReleased += Mapper.getReleasedRangesCount();
      Sci->ReleaseInfo.LastReleasedBytes = Mapper.getReleasedBytes();
    }
    Sci->ReleaseInfo.LastReleaseAtNs = getMonotonicTime();
  }

  SizeClassInfo SizeClassInfoArray[NumClasses];

  ByteMap PossibleRegions;
  // Keep track of the lowest & highest regions allocated to avoid looping
  // through the whole NumRegions.
  uptr MinRegionIndex;
  uptr MaxRegionIndex;
  s32 ReleaseToOsIntervalMs;
  // Unless several threads request regions simultaneously from different size
  // classes, the stash rarely contains more than 1 entry.
  static constexpr uptr MaxStashedRegions = 4;
  StaticSpinMutex RegionsStashMutex;
  uptr NumberOfStashedRegions;
  uptr RegionsStash[MaxStashedRegions];
};

} // namespace scudo

#endif // SCUDO_PRIMARY32_H_
