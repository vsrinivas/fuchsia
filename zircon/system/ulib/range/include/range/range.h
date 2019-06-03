// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_RANGE_RANGE_H_
#define ZIRCON_SYSTEM_ULIB_RANGE_RANGE_H_

#include <limits>

#include <lib/fit/result.h>
#include <zircon/errors.h>
#include <zircon/types.h>

namespace range {

// Range is a half closed interval [start, end).
template <typename T> class Range {
    static_assert(std::is_unsigned_v<T>);

public:
    // Creates Range from [start, end).
    Range(T start, T end);

    Range() = delete;
    Range(const Range&) = default;
    Range& operator=(const Range&) = default;
    Range& operator=(Range&&) = default;
    bool operator==(Range const&) const;
    bool operator!=(Range const&) const;
    ~Range() = default;

    T start() const { return start_; }

    // The length of the range is end - start. When start => end, then length is
    // considered as zero.
    T length() const;

    T end() const { return end_; }

private:
    // start of the range
    T start_;

    // end of the range. End in non-inclusive.
    T end_ = {};
};

// Returns true if two extents overlap.
template <typename T> bool Overlap(const Range<T>& x, const Range<T>& y);

// Returns true if two extents are adjacent. Two ranges are considered adjacent
// if one range starts right after another ends i.e. [a, b) [b, c] are
// adjacent ranges where a < b < c.
template <typename T> bool Adjacent(const Range<T>& x, const Range<T>& y);

// Two ranges are mergable is they either overlap or are adjacent.
template <typename T> bool Mergable(const Range<T>& x, const Range<T>& y);

// Merges two mergable extents into one and returns the merged extent.
// Returns an error on failure to Create new Range from merged range. See Create().
template <typename T>
fit::result<Range<T>, zx_status_t> Merge(const Range<T>& x, const Range<T>& y);

} // namespace range

#endif // ZIRCON_SYSTEM_ULIB_RANGE_RANGE_H_
