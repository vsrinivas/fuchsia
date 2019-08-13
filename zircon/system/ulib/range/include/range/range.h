// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RANGE_RANGE_H_
#define RANGE_RANGE_H_

#include <lib/fit/result.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <algorithm>
#include <limits>

namespace range {

// Range is a half closed interval [start, end).
template <typename T>
class Range {
  static_assert(std::is_unsigned_v<T>);

 public:
  // Creates Range from [start, end).
  Range(T start, T end) : start_(start), end_(end) {}

  Range() = delete;
  Range(const Range&) = default;
  Range& operator=(const Range&) = default;
  Range& operator=(Range&&) = default;
  bool operator==(Range const& y) const { return (start() == y.start()) && (end() == y.end()); }
  bool operator!=(Range const& y) const { return (start() != y.start()) || (end() != y.end()); }
  ~Range() = default;

  T start() const { return start_; }

  // The length of the range is end - start. When start => end, then length is
  // considered as zero.
  T length() const {
    if (end_ <= start_) {
      return T(0);
    }
    return end_ - start_;
  }

  T end() const { return end_; }

 private:
  // start of the range
  T start_;

  // end of the range. End in non-inclusive.
  T end_ = {};
};

// Returns true if two extents overlap.
template <typename T>
bool Overlap(const Range<T>& x, const Range<T>& y) {
  if (x.length() == 0 || y.length() == 0) {
    return false;
  }

  T max_start = std::max(x.start(), y.start());
  T min_end = std::min(x.end(), y.end());

  return max_start < min_end;
}

// Returns true if two extents are adjacent. Two ranges are considered adjacent
// if one range starts right after another ends i.e. [a, b) [b, c] are
// adjacent ranges where a < b < c.
template <typename T>
bool Adjacent(const Range<T>& x, const Range<T>& y) {
  if (x.length() == 0 || y.length() == 0) {
    return false;
  }

  if (Overlap(x, y)) {
    return false;
  }

  T max_start = std::max(x.start(), y.start());
  T min_end = std::min(x.end(), y.end());

  return max_start == min_end;
}

// Two ranges are mergable is they either overlap or are adjacent.
template <typename T>
bool Mergable(const Range<T>& x, const Range<T>& y) {
  return Adjacent(x, y) || Overlap(x, y);
}

// Merges two mergable extents into one and returns the merged extent.
// Returns an error on failure to Create new Range from merged range. See Create().
template <typename T>
fit::result<Range<T>, zx_status_t> Merge(const Range<T>& x, const Range<T>& y) {
  if (!Mergable(x, y)) {
    return fit::error<zx_status_t>(ZX_ERR_OUT_OF_RANGE);
  }

  T merged_start = std::min(x.start(), y.start());
  T merged_end = std::max(x.end(), y.end());

  return fit::ok(Range<T>(merged_start, merged_end));
}

extern template class Range<uint64_t>;
extern template bool Overlap<uint64_t>(const Range<uint64_t>& x, const Range<uint64_t>& y);
extern template bool Adjacent<uint64_t>(const Range<uint64_t>& x, const Range<uint64_t>& y);
extern template bool Mergable<uint64_t>(const Range<uint64_t>& x, const Range<uint64_t>& y);
extern template fit::result<Range<uint64_t>, zx_status_t> Merge(const Range<uint64_t>& x,
                                                                const Range<uint64_t>& y);

}  // namespace range

#endif  // RANGE_RANGE_H_
