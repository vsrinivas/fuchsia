//===-- size_class_map.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_SIZE_CLASS_MAP_H_
#define SCUDO_SIZE_CLASS_MAP_H_

#include "common.h"
#include "string_utils.h"

namespace scudo {

template <u8 NumBits, u8 MinSizeLog, u8 MidSizeLog, u8 MaxSizeLog,
          u32 MaxNumCachedHintT, u8 MaxBytesCachedLog>
class SizeClassMap {
  static const uptr MinSize = 1U << MinSizeLog;
  static const uptr MidSize = 1U << MidSizeLog;
  static const uptr MidClass = MidSize / MinSize;
  static const u8 S = NumBits - 1;
  static const uptr M = (1U << S) - 1;

public:
  static const u32 MaxNumCachedHint = MaxNumCachedHintT;

  static const uptr MaxSize = 1UL << MaxSizeLog;
  static const uptr NumClasses =
      MidClass + ((MaxSizeLog - MidSizeLog) << S) + 1;
  COMPILER_CHECK(NumClasses <= 256);
  static const uptr LargestClassId = NumClasses - 1;
  static const uptr BatchClassId = 0;

  static uptr getSizeByClassId(uptr ClassId) {
    DCHECK_NE(ClassId, BatchClassId);
    if (ClassId <= MidClass)
      return ClassId << MinSizeLog;
    ClassId -= MidClass;
    const uptr T = MidSize << (ClassId >> S);
    return T + (T >> S) * (ClassId & M);
  }

  static uptr getClassIdBySize(uptr Size) {
    DCHECK_LE(Size, MaxSize);
    if (Size <= MidSize)
      return (Size + MinSize - 1) >> MinSizeLog;
    const uptr L = getMostSignificantSetBitIndex(Size);
    const uptr HBits = (Size >> (L - S)) & M;
    const uptr LBits = Size & ((1U << (L - S)) - 1);
    const uptr L1 = L - MidSizeLog;
    return MidClass + (L1 << S) + HBits + (LBits > 0);
  }

  static u32 getMaxCachedHint(uptr Size) {
    DCHECK_LE(Size, MaxSize);
    DCHECK_NE(Size, 0);
    u32 N;
    // Force a 32-bit division if the template parameters allow for it.
    if (MaxBytesCachedLog > 31 || MaxSizeLog > 31)
      N = static_cast<u32>((1UL << MaxBytesCachedLog) / Size);
    else
      N = (1U << MaxBytesCachedLog) / static_cast<u32>(Size);
    return Max(1U, Min(MaxNumCachedHint, N));
  }

  static void print() {
    uptr PrevS = 0;
    uptr TotalCached = 0;
    for (uptr I = 0; I < NumClasses; I++) {
      if (I == BatchClassId)
        continue;
      const uptr S = getSizeByClassId(I);
      if (S >= MidSize / 2 && (S & (S - 1)) == 0)
        Printf("\n");
      uptr D = S - PrevS;
      uptr P = PrevS ? (D * 100 / PrevS) : 0;
      uptr L = S ? getMostSignificantSetBitIndex(S) : 0;
      const uptr Cached = getMaxCachedHint(S) * S;
      Printf(
          "C%02zd => S: %zd diff: +%zd %02zd%% L %zd Cached: %zd %zd; id %zd\n",
          I, getSizeByClassId(I), D, P, L, getMaxCachedHint(S), Cached,
          getClassIdBySize(S));
      TotalCached += Cached;
      PrevS = S;
    }
    Printf("Total Cached: %zd\n", TotalCached);
  }

  static void validate() {
    for (uptr C = 0; C < NumClasses; C++) {
      if (C == BatchClassId)
        continue;
      const uptr S = getSizeByClassId(C);
      CHECK_NE(S, 0U);
      CHECK_EQ(getClassIdBySize(S), C);
      if (C < LargestClassId)
        CHECK_EQ(getClassIdBySize(S + 1), C + 1);
      CHECK_EQ(getClassIdBySize(S - 1), C);
      CHECK_GT(getSizeByClassId(C), getSizeByClassId(C - 1));
    }
    for (uptr S = 1; S <= MaxSize; S++) {
      const uptr C = getClassIdBySize(S);
      CHECK_LT(C, NumClasses);
      CHECK_GE(getSizeByClassId(C), S);
      if (C > 0)
        CHECK_LT(getSizeByClassId(C - 1), S);
    }
  }
};

typedef SizeClassMap<3, 5, 8, 17, 8, 10> DefaultSizeClassMap;

// TODO(kostyak): figure out what works best for Android & Fuchsia
#if SCUDO_WORDSIZE == 64U
typedef SizeClassMap<3, 5, 8, 15, 8, 10> SvelteSizeClassMap;
typedef SizeClassMap<3, 5, 8, 16, 14, 12> AndroidSizeClassMap;
#else
typedef SizeClassMap<3, 4, 7, 15, 8, 10> SvelteSizeClassMap;
typedef SizeClassMap<3, 4, 7, 16, 14, 12> AndroidSizeClassMap;
#endif

} // namespace scudo

#endif // SCUDO_SIZE_CLASS_MAP_H_
