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

#include <cstddef>
#include <string_view>

namespace memalloc {

// Serializes ranges from up to two spans in lexicographic order.
class MemRangeStream {
 public:
  // Will lexicographically sort the ranges.
  explicit MemRangeStream(cpp20::span<MemRange> ranges, cpp20::span<MemRange> aux_ranges = {})
      : ranges_(ranges),          //
        it_(ranges.begin()),      //
        aux_ranges_(aux_ranges),  //
        aux_it_(aux_ranges.begin()) {
    std::sort(ranges.begin(), ranges.end());
    std::sort(aux_ranges.begin(), aux_ranges.end());
  }

  // Returns the next range in the stream, returning nullptr when all ranges
  // have been streamed (until the stream itself has been reset).
  const MemRange* operator()();

  constexpr size_t size() const { return ranges_.size() + aux_ranges_.size(); }

  constexpr bool empty() const { return size() == 0; }

 private:
  using iterator = typename cpp20::span<MemRange>::iterator;

  cpp20::span<MemRange> ranges_;
  iterator it_;
  cpp20::span<MemRange> aux_ranges_;
  iterator aux_it_;
};

// Inlined for phys-compatibility, where there is no ambient heap.
using MemRangeCallback = fit::inline_function<bool(const MemRange&)>;

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

// A variant of the above that finds all of the normalized ranges in order.
// It also runs in O(n*log(n)) time but with O(n) space. In particular, a
// buffer of scratch must be provided that is at least
// least 4*n*sizeof(void*) in size.
void FindNormalizedRanges(MemRangeStream ranges, cpp20::span<void*> scratch, MemRangeCallback cb);

}  // namespace memalloc

#endif  // ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_ALGORITHM_H_
