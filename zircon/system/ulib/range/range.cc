// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <limits>

#include <range/range.h>
#include <zircon/assert.h>

namespace range {

template <typename T> Range<T>::Range(T start, T end) {
    start_ = start;
    end_ = end;
}

template <typename T> bool Range<T>::operator==(Range<T> const& y) const {
    return (start() == y.start()) && (end() == y.end());
}

template <typename T> bool Range<T>::operator!=(Range<T> const& y) const {
    return (start() != y.start()) || (end() != y.end());
}

template <typename T> T Range<T>::length() const {
    if (end_ <= start_) {
        return T(0);
    }
    return end_ - start_;
}

template <typename T> bool Overlap(const Range<T>& x, const Range<T>& y) {
    if (x.length() == 0 || y.length() == 0) {
        return false;
    }

    T max_start = std::max(x.start(), y.start());
    T min_end = std::min(x.end(), y.end());

    return max_start < min_end;
}

template <typename T> bool Adjacent(const Range<T>& x, const Range<T>& y) {
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

template <typename T> bool Mergable(const Range<T>& x, const Range<T>& y) {
    return Adjacent(x, y) || Overlap(x, y);
}

template <typename T>
fit::result<Range<T>, zx_status_t> Merge(const Range<T>& x, const Range<T>& y) {

    if (!Mergable(x, y)) {
        return fit::error<zx_status_t>(ZX_ERR_OUT_OF_RANGE);
    }

    T merged_start = std::min(x.start(), y.start());
    T merged_end = std::max(x.end(), y.end());

    return fit::ok(Range<T>(merged_start, merged_end));
}

template class Range<uint64_t>;
template bool Overlap<uint64_t>(const Range<uint64_t>& x, const Range<uint64_t>& y);
template bool Adjacent<uint64_t>(const Range<uint64_t>& x, const Range<uint64_t>& y);
template bool Mergable<uint64_t>(const Range<uint64_t>& x, const Range<uint64_t>& y);
template fit::result<Range<uint64_t>, zx_status_t> Merge(const Range<uint64_t>& x,
                                                         const Range<uint64_t>& y);

} // namespace range
