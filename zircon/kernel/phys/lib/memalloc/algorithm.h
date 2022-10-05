// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_ALGORITHM_H_
#define ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_ALGORITHM_H_

#include <lib/fit/function.h>
#include <lib/fit/result.h>
#include <lib/memalloc/range.h>
#include <lib/stdcompat/span.h>
#include <zircon/assert.h>

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace memalloc {

// Inlined for phys-compatibility, where there is no ambient heap.
using RangeCallback = fit::inline_function<bool(const Range&), sizeof(void*) * 8>;

// Serializes ranges in lexicographic order from a variable number of Range arrays.
class RangeStream {
 public:
  // Assumes that each associated array is already in lexicographic ordered.
  explicit RangeStream(cpp20::span<internal::RangeIterationContext> state) : state_(state) {
    ZX_DEBUG_ASSERT(std::all_of(state.begin(), state.end(), [](const auto& ctx) {
      return std::is_sorted(ctx.ranges_.begin(), ctx.ranges_.end());
    }));
  }

  // Returns the next range in the stream, returning nullptr when all ranges
  // have been streamed (until the stream itself has been reset).
  const Range* operator()();

  size_t size() const {
    size_t size = 0;
    for (const auto& ctx : state_) {
      size += ctx.ranges_.size();
    }
    return size;
  }

  bool empty() const { return size() == 0; }

  // Reset the head of the stream back to the beginning.
  void reset() {
    for (auto& ctx : state_) {
      ctx.it_ = ctx.ranges_.begin();
    }
  }

 private:
  cpp20::span<internal::RangeIterationContext> state_;
};

// Say a range among a set is "normalized" if it does not intersect with any of
// them and it is maximally contiguous. This routine finds the normalized RAM
// ranges among a provided set with a degree of arbitrary intersection with one
// another. The range is emitted by passing it to a callback for processing.
// If the callback returns false, then the routine will exit early.
//
// The span of ranges will be reordered, but otherwise will be preserved.
//
// This function runs in O(n*log(n)) time, where n is the total number of given
// ranges.
//
void FindNormalizedRamRanges(RangeStream ranges, RangeCallback cb);

// Used for streamlined testing.
inline void FindNormalizedRamRanges(cpp20::span<Range> ranges, RangeCallback cb) {
  internal::RangeIterationContext ctx(ranges);
  FindNormalizedRamRanges(RangeStream({&ctx, 1}), std::move(cb));
}

// The size of tne void* scratch space needed for FindNormalizedRanges() below,
// where `n` is the size of the input RangeStream.
constexpr size_t FindNormalizedRangesScratchSize(size_t n) { return 4 * n; }

// A variant of the above algorithm that finds all of the normalized ranges in
// order. It also runs in O(n*log(n)) time but with O(n) space. In particular,
// a void* buffer of scratch of size `FindNormalizedRangesScratchSize()` must
// be.
//
// Ranges may overlap only if they are of the same type or one type is
// kFreeRam; otherwise fit::failed is returned.
fit::result<fit::failed> FindNormalizedRanges(RangeStream ranges, cpp20::span<void*> scratch,
                                              RangeCallback cb);

// Used for streamlined testing.
inline fit::result<fit::failed> FindNormalizedRanges(cpp20::span<Range> ranges,
                                                     cpp20::span<void*> scratch, RangeCallback cb) {
  internal::RangeIterationContext ctx(ranges);
  return FindNormalizedRanges(RangeStream({&ctx, 1}), scratch, std::move(cb));
}

}  // namespace memalloc

#endif  // ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_ALGORITHM_H_
