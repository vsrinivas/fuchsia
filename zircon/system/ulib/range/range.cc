// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cinttypes>
#include <cstddef>

#include <range/range.h>

namespace range {

template class Range<uint64_t>;
template bool Overlap<uint64_t>(const Range<uint64_t>& x, const Range<uint64_t>& y);
template bool Adjacent<uint64_t>(const Range<uint64_t>& x, const Range<uint64_t>& y);
template bool Mergable<uint64_t>(const Range<uint64_t>& x, const Range<uint64_t>& y);
template fit::result<Range<uint64_t>, zx_status_t> Merge(const Range<uint64_t>& x,
                                                         const Range<uint64_t>& y);

}  // namespace range
