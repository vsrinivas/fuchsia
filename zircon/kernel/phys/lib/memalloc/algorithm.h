// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_ALGORITHM_H_
#define ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_ALGORITHM_H_

#include <lib/fit/function.h>
#include <lib/memalloc/range.h>
#include <lib/stdcompat/span.h>
#include <zircon/assert.h>

#include <cstddef>
#include <string_view>

namespace memalloc {

// Inlined for phys-compatibility, where there is no ambient heap.
using MemRangeCallback = fit::inline_function<bool(const MemRange&)>;

// Serializes ranges in lexicographic order from a variable number of MemRange arrays.
class MemRangeStream {
 public:
  // Assumes that each associated array is already in lexicographic ordered.
  explicit MemRangeStream(cpp20::span<internal::MemRangeIterationContext> state) : state_(state) {
    ZX_DEBUG_ASSERT(std::all_of(state.begin(), state.end(), [](const auto& ctx) {
      return std::is_sorted(ctx.ranges_.begin(), ctx.ranges_.end());
    }));
  }

  // Returns the next range in the stream, returning nullptr when all ranges
  // have been streamed (until the stream itself has been reset).
  const MemRange* operator()();

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
  cpp20::span<internal::MemRangeIterationContext> state_;
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
void FindNormalizedRamRanges(MemRangeStream ranges, MemRangeCallback cb);

// Used for streamlined testing.
inline void FindNormalizedRamRanges(cpp20::span<MemRange> ranges, MemRangeCallback cb) {
  internal::MemRangeIterationContext ctx(ranges);
  FindNormalizedRamRanges(MemRangeStream({&ctx, 1}), std::move(cb));
}

// A variant of the above algorithm that finds all of the normalized ranges in
// order. It also runs in O(n*log(n)) time but with O(n) space. In particular,
// a buffer of scratch must be provided that is at least 4*n*sizeof(void*) in
// size in bytes.
void FindNormalizedRanges(MemRangeStream ranges, cpp20::span<void*> scratch, MemRangeCallback cb);

// Used for streamlined testing.
inline void FindNormalizedRanges(cpp20::span<MemRange> ranges, cpp20::span<void*> scratch,
                                 MemRangeCallback cb) {
  internal::MemRangeIterationContext ctx(ranges);
  FindNormalizedRanges(MemRangeStream({&ctx, 1}), scratch, std::move(cb));
}

}  // namespace memalloc

#endif  // ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_ALGORITHM_H_
