// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_INCLUDE_KTL_ALGORITHM_H_
#define ZIRCON_KERNEL_INCLUDE_KTL_ALGORITHM_H_

#include <algorithm>

namespace ktl {

// "Sorting operations" (subset)
using std::stable_sort;

// "Minimum/maximum operations"
using std::clamp;
using std::max;
using std::max_element;
using std::min;
using std::min_element;
using std::minmax;
using std::minmax_element;

}  // namespace ktl

#endif  // ZIRCON_KERNEL_INCLUDE_KTL_ALGORITHM_H_
