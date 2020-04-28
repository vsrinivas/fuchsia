// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <range/range.h>

namespace range {

template class Range<uint64_t>;
template bool Overlap<Range<uint64_t>>(const Range<uint64_t>& x, const Range<uint64_t>& y);
template bool Adjacent<Range<uint64_t>>(const Range<uint64_t>& x, const Range<uint64_t>& y);
template bool Mergable<Range<uint64_t>>(const Range<uint64_t>& x, const Range<uint64_t>& y);

}  // namespace range
