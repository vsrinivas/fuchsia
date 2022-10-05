// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "algorithm.h"

#include <lib/fit/result.h>
#include <lib/memalloc/range.h>
#include <lib/stdcompat/span.h>
#include <zircon/assert.h>

#include <algorithm>
#include <array>
#include <limits>

namespace memalloc {

constexpr uint64_t kMax = std::numeric_limits<uint64_t>::max();

namespace {

constexpr uint64_t GetLeft(const Range& range) { return range.addr; }

constexpr uint64_t GetRight(const Range& range) {
  return GetLeft(range) + std::min(kMax - GetLeft(range), range.size);
}

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

  constexpr Interval(const Range& range) : Interval(GetLeft(range), GetRight(range)) {}

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

  constexpr Range AsRamRange() const {
    return {.addr = Left(), .size = Right() - Left(), .type = Type::kFreeRam};
  }

 private:
  uint64_t left_ = 0;
  uint64_t right_ = 0;
};

enum class EndpointType { kLeft, kRight };

struct Endpoint {
  const Range* range;
  EndpointType type;

  constexpr bool IsLeft() const { return type == EndpointType::kLeft; }

  constexpr uint64_t value() const {
    ZX_DEBUG_ASSERT(range);
    return IsLeft() ? GetLeft(*range) : GetRight(*range);
  }

  // Compares by value, giving left endpoints precedence.
  constexpr bool operator<(Endpoint other) const {
    return value() < other.value() || (value() == other.value() && IsLeft() && !other.IsLeft());
  }
};

// An array of counters indexed by Type.
class ActiveRanges {
 public:
  size_t& operator[](Type type) {
    uint64_t type_val = static_cast<uint64_t>(type);
    switch (type_val) {
      case ZBI_MEM_RANGE_RAM:
        return values_[0];
      case ZBI_MEM_RANGE_PERIPHERAL:
        return values_[1];
      case ZBI_MEM_RANGE_RESERVED:
        return values_[2];
      case kMinExtendedTypeValue ... kMaxExtendedTypeValue - 1:
        static_assert(kNumBaseTypes == 3);
        return values_[kNumBaseTypes + static_cast<size_t>(type_val - kMinExtendedTypeValue)];
    }
    // Normalize to kReserved if unknown.
    return (*this)[Type::kReserved];
  }

  // Gives the active range type with the highest relative precedence,
  // std::nullopt if there are no active ranges, or fit::failed if
  // * two different extended types are active
  // * both an extended type and one of kReserved or kPeripheral are active.
  fit::result<fit::failed, std::optional<Type>> DominantType() {
    // First look through the base types in reverse-order of precedence (so
    // "last wins").
    std::optional<Type> active_base;
    for (Type type : {Type::kFreeRam, Type::kPeripheral, Type::kReserved}) {
      if ((*this)[type] > 0) {
        active_base = type;
      }
    }

    std::optional<Type> active_extended;
    for (uint64_t i = kMinExtendedTypeValue; i < kMaxExtendedTypeValue; ++i) {
      Type type = static_cast<Type>(i);
      if ((*this)[type] > 0) {
        // If there is a non-kFreeRam base type or another extended type
        // active, that's an error.
        if ((active_base && *active_base != Type::kFreeRam) || active_extended) {
          return fit::failed();
        }
        active_extended = type;
      }
    }

    // Give an active extended type precedence, as we now know that it can only
    // carve out subranges of active free RAM.
    if (active_extended) {
      return fit::ok(*active_extended);
    }
    if (active_base) {
      return fit::ok(*active_base);
    }
    return fit::ok(std::nullopt);
  }

 private:
  std::array<size_t, kNumBaseTypes + kNumExtendedTypes> values_ = {};
};

}  // namespace

const Range* RangeStream::operator()() {
  static constexpr auto at_end = [](const auto& ctx) { return ctx.it_ == ctx.ranges_.end(); };

  // Dereferencing a cpp20::span iterator returns a reference.
  constexpr auto to_ptr = [](auto it) -> const Range* { return &(*it); };

  // We want to take the lexicographic minimum among the ranges currently
  // pointed to by the context.
  auto state_it =
      std::min_element(state_.begin(), state_.end(), [](const auto& ctx_a, const auto& ctx_b) {
        // Take any non-'end' iterator to be strictly less than any 'end' one.
        if (at_end(ctx_a)) {
          return false;
        }
        if (at_end(ctx_b)) {
          return !at_end(ctx_a);
        }
        return *ctx_a.it_ < *ctx_b.it_;
      });
  if (state_it == state_.end() || at_end(*state_it)) {
    return nullptr;
  }
  return to_ptr(state_it->it_++);
}

void FindNormalizedRamRanges(RangeStream ranges, RangeCallback cb) {
  // Having sorted lexicographically on range endpoints (as RangeStream
  // does) is crucial to the following logic. With this ordering, given a range
  // of interest, the moment we come across a range disjoint from it, we know
  // that all subsequent ranges will similarly be disjoint. This allows us to
  // straightforwardly disambiguate the contiguous regions among
  // arbitrarily-overlapping ranges.

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
  for (const Range* range = ranges(); range != nullptr; range = ranges()) {
    Interval interval(*range);
    if (interval.empty()) {
      continue;
    }

    if (range->type == Type::kFreeRam) {
      // Check to see if this new RAM interval intersects with the current
      // non-RAM interval we're tracking. If they intersect, it would be at
      // the head of the new interval; if so, update the new interval to just
      // cover the tail.
      if (interval.IntersectsWith(current_non_ram)) {
        ZX_DEBUG_ASSERT(interval.HeadBeforeIntersection(current_non_ram).empty());
        interval = interval.TailAfterIntersection(current_non_ram);
        if (interval.empty()) {
          continue;
        }
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

fit::result<fit::failed> FindNormalizedRanges(RangeStream ranges, cpp20::span<void*> scratch,
                                              RangeCallback cb) {
  if (ranges.empty()) {
    return fit::ok();
  }

  // This algorithm relies on creating a sorted array of endpoints. For every
  // range, we need two endpoints, each of which we represent with two words.
  {
    static_assert(std::alignment_of_v<void*> % std::alignment_of_v<Endpoint> == 0);
    static_assert(sizeof(Endpoint) == 2 * sizeof(void*));
    const size_t min_size_bytes = FindNormalizedRangesScratchSize(ranges.size()) * sizeof(void*);
    ZX_ASSERT_MSG(scratch.size_bytes() >= min_size_bytes,
                  "scratch space must be at least 4*sizeof(void*) times the number of ranges "
                  "(%zu) in bytes: expected >= %zu bytes; got %zu bytes",
                  ranges.size(), min_size_bytes, scratch.size_bytes());
  }

  cpp20::span<Endpoint> endpoints{reinterpret_cast<Endpoint*>(scratch.data()), 2 * ranges.size()};
  for (size_t i = 0; i < ranges.size(); ++i) {
    const Range* range = ranges();
    ZX_DEBUG_ASSERT(range);
    endpoints[2 * i] = {range, EndpointType::kLeft};
    endpoints[2 * i + 1] = {range, EndpointType::kRight};
  }
  std::sort(endpoints.begin(), endpoints.end());

  // The following algorithm is simple, but rather subtle. It works as follows.
  //
  // We iterate through endpoints sorted by value and maintain counters that
  // gives the number of the original ranges that are 'active' at this point in
  // time: if we see a left endpoint, the associated counter is incremented; if
  // we see a right endpoint, it is decremented. We also maintain the the type
  // and start value of the normalized range we are currently building up.
  //
  // After processing each endpoint of a specific value, we take stock of the
  // counters: every positive counter corresponds to a collection of active
  // ranges of that associated type. If there are active ranges, let TYPE be
  // the most dominant among them (i.e., with the highest relative precedence):
  // then we are either in the process of building up a normalized TYPE range
  // or have just started to; if the former, then carry on; if the latter, it
  // is time to emit the previous normalized range we had been building up, as
  // we have just found its end. If all counters are zero, we are no longer
  // building up a normalized range and should clear the tracked start and type.
  ActiveRanges counters;
  std::optional<Type> curr_type;
  uint64_t curr_start = endpoints.front().value();
  uint64_t curr_value = curr_start;

  // Processes an "event", given by seeing a new active, normalized range type.
  // If std::nullopt, there are no active ranges at this time.
  auto process_event = [&](std::optional<Type> active_type) -> bool {
    // Still building up the same range.
    if (active_type == curr_type) {
      return true;
    }

    // If we have been building up a (non-empty) normalized range of a
    // different type, emit it.
    ZX_DEBUG_ASSERT(curr_start <= curr_value);
    if (curr_type && curr_start < curr_value) {
      if (!cb({.addr = curr_start, .size = curr_value - curr_start, .type = *curr_type})) {
        return false;
      }
    }
    curr_start = curr_value;
    curr_type = active_type;
    return true;
  };

  for (size_t i = 0; i < endpoints.size();) {
    curr_value = endpoints[i].value();
    do {
      const Endpoint& endpoint = endpoints[i];
      ZX_DEBUG_ASSERT(endpoint.range);
      const Type type = endpoint.range->type;
      if (endpoint.IsLeft()) {
        counters[type]++;
      } else {
        ZX_DEBUG_ASSERT(counters[type] > 0);
        counters[type]--;
      }
      ++i;
    } while (i < endpoints.size() && endpoints[i].value() == curr_value);

    auto result = counters.DominantType();
    if (result.is_error()) {
      return result.take_error();
    }
    if (!process_event(std::move(result).value())) {
      return fit::ok();
    }
  }
  // There should be no active ranges tracked now, normalized or otherwise.
  ZX_DEBUG_ASSERT(!curr_type);
  ZX_DEBUG_ASSERT(counters.DominantType().is_ok());
  ZX_DEBUG_ASSERT(!counters.DominantType().value());
  return fit::ok();
}

}  // namespace memalloc
