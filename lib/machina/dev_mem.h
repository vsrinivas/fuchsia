// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_DEV_MEM_H_
#define GARNET_LIB_MACHINA_DEV_MEM_H_

#include <zircon/types.h>

#include <set>

namespace machina {

class DevMem {
 public:
  static constexpr zx_gpaddr_t kAddrLowerBound = 0xc00000000;
  static constexpr zx_gpaddr_t kAddrUpperBound = 0x1000000000;

  struct Range {
    zx_gpaddr_t addr;
    size_t size;

    bool operator<(const Range& r) const { return addr + size <= r.addr; }

    bool contains(const Range& r) const { return !(r < *this) && !(*this < r); }
  };
  static constexpr Range kAddrLowerRange = Range{0, kAddrLowerBound};
  static constexpr Range kAddrHigherRange =
      Range{kAddrUpperBound, SIZE_MAX - kAddrUpperBound};
  using RangeSet = std::set<Range>;

  bool AddRange(zx_gpaddr_t addr, size_t size) {
    Range candidate = Range{addr, size};
    if (size == 0 || kAddrLowerRange.contains(candidate) ||
        kAddrHigherRange.contains(candidate)) {
      return false;
    } else {
      return ranges.emplace(candidate).second;
    }
  }

  const RangeSet::const_iterator begin() const { return ranges.begin(); }
  const RangeSet::const_iterator end() const { return ranges.end(); }

  // Generates, by calling the provided functor, all Range's that are in the
  // provided range, that do not overlap with any internal ranges. This means
  // the generated set is precisely the inverse of our contained ranges, unioned
  // with the provided range.
  template<typename F>
  void YieldInverseRange(zx_gpaddr_t base, size_t size, F yield) const {
    zx_gpaddr_t prev = base;
    for (const auto& range: ranges) {
      zx_gpaddr_t next_top = std::min(range.addr, base + size);
      if (next_top > prev) {
        yield(Range{.addr = prev, .size = next_top - prev});
      }
      prev = range.addr + range.size;
    }
    zx_gpaddr_t next_top = std::min(prev, base + size);
    if (next_top > prev) {
      yield(Range{.addr = prev, .size = next_top - prev});
    }
  }

 private:
  RangeSet ranges;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_DEV_MEM_H_
