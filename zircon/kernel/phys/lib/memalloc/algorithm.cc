// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "algorithm.h"

#include <lib/memalloc/range.h>
#include <lib/stdcompat/span.h>
#include <zircon/assert.h>

#include <algorithm>
#include <limits>

namespace memalloc {

constexpr uint64_t kMax = std::numeric_limits<uint64_t>::max();

namespace {

// Represents a 64-bit, unsigned integral interval, [Left(), Right()), whose
// inclusive endpoints may may range from 0 to UINT64_MAX -1.
//
// For arithmetic and overflow safety convenience, we take the right endpoint
// be exclusive, which is what disallows UINT64_MAX from being an endpoint.
// Though this limitation is unfortunate, it is not an issue in practice, as
// this type is used to represent a range of the physical address space and
// supported architectures in turn do not support addresses that high.
//
// The "empty" interval is represented as the only Interval with Left() ==
// Right(), which, by convention is taken to be rooted at 0.
class Interval {
 public:
  // Gives the empty interval.
  constexpr Interval() = default;

  // Gives the interval [left, right) when left < right, and the empty interval
  // otherwise.
  constexpr Interval(uint64_t left, uint64_t right)
      : left_(left >= right ? 0 : left), right_(left >= right ? 0 : right) {}

  constexpr Interval(const MemRange& range)
      : Interval(range.addr, range.addr + std::min(kMax - range.addr, range.size)) {}

  constexpr bool empty() const { return Left() == Right(); }

  constexpr uint64_t Left() const { return left_; }

  constexpr uint64_t Right() const { return right_; }

  constexpr bool IsAdjacentTo(Interval other) const {
    return Left() == other.Right() || other.Left() == Right();
  }

  constexpr bool IntersectsWith(Interval other) const {
    return !empty() && !other.empty() && Left() < other.Right() && Right() > other.Left();
  }

  // Returns the subrange before the intersection with another interval.
  constexpr Interval HeadBeforeIntersection(Interval other) const {
    ZX_DEBUG_ASSERT(IntersectsWith(other));
    return {Left(), std::max(Left(), other.Left())};
  }

  // Returns the subrange after the intersection with another interval.
  constexpr Interval TailAfterIntersection(Interval other) const {
    ZX_DEBUG_ASSERT(IntersectsWith(other));
    return {std::min(Right(), other.Right()), Right()};
  }

  constexpr void MergeInto(Interval other) {
    ZX_DEBUG_ASSERT((empty() || other.empty()) || IntersectsWith(other) || IsAdjacentTo(other));
    if (other.empty()) {
      return;
    }
    left_ = empty() ? other.left_ : std::min(Left(), other.Left());
    right_ = empty() ? other.right_ : std::max(Right(), other.Right());
  }

  constexpr MemRange AsRamRange() const {
    return {.addr = Left(), .size = Right() - Left(), .type = Type::kFreeRam};
  }

 private:
  uint64_t left_ = 0;
  uint64_t right_ = 0;
};

}  // namespace

void FindNormalizedRamRanges(cpp20::span<MemRange> ranges, MemRangeCallback cb) {
  // Sorting lexicographically on range/interval endpoints is crucial to the
  // following logic. With this ordering, given a range of interest, the moment
  // we come across a range disjoint from it, we know that all subsequent
  // ranges will similarly be disjoint. This allows us to straightforwardly
  // disambiguate the contiguous regions among arbitrarily-overlapping ranges.
  std::sort(ranges.begin(), ranges.end());

  // The current RAM range of interest. With each new RAM range we come across,
  // we see if it can be merged into the candidate and update accordingly; with
  // each new non-RAM range, we see if it intersects with the candidate and
  // truncate accordingly. Once we know that subsequent ranges are disjoint, we
  // know that the candidate is contiguous and we see if it meets our
  // constraints; otherwise we move onto the next one.
  Interval candidate;
  // Tracks the last contiguous range of memory that is the union of all
  // non-RAM types.
  Interval current_non_ram;
  for (const MemRange& range : ranges) {
    Interval interval(range);
    if (interval.empty()) {
      continue;
    }

    if (range.type == Type::kFreeRam) {
      // Check to see if this new RAM interval intersects with the current
      // non-RAM interval we're tracking. If they intersect, it would be at
      // the head of the new interval; if so, update the new interval to just
      // cover the tail.
      if (interval.IntersectsWith(current_non_ram)) {
        ZX_DEBUG_ASSERT(interval.HeadBeforeIntersection(current_non_ram).empty());
        interval = interval.TailAfterIntersection(current_non_ram);
      }

      // Merge the new RAM range into the current candidate if possible.
      if (candidate.IntersectsWith(interval) || candidate.IsAdjacentTo(interval)) {
        candidate.MergeInto(interval);
      } else {
        // Found new, disjoint RAM interval. The candidate is guaranteed to not
        // intersect with any subsequent ranges. Emit and move on.
        if (!candidate.empty() && !cb(candidate.AsRamRange())) {
          return;
        }
        candidate = interval;
      }
    } else {
      // Check to see if the candidate RAM intersects with this new non-RAM
      // interval. If it does, emit the pre-intersection head and then update
      // the candidate as the post-intersection tail.
      if (candidate.IntersectsWith(interval)) {
        Interval before = candidate.HeadBeforeIntersection(interval);
        if (!before.empty() && !cb(before.AsRamRange())) {
          return;
        }
        candidate = candidate.TailAfterIntersection(interval);
      }

      if (current_non_ram.IntersectsWith(interval) || current_non_ram.IsAdjacentTo(interval)) {
        current_non_ram.MergeInto(interval);
      } else {
        current_non_ram = interval;
      }
    }
  }

  // There are no more ranges. Since we compared each new RAM interval against the
  // the current non-RAM interval and each new non-RAM interval against the
  // candidate, refitting as we went, we can be sure that the remaining
  // candidate is disjoint from the remaining non-RAM interval (% emptiness).
  ZX_DEBUG_ASSERT(candidate.empty() || current_non_ram.empty() ||
                  !candidate.IntersectsWith(current_non_ram));
  if (!candidate.empty()) {
    cb(candidate.AsRamRange());
  }
}

}  // namespace memalloc
