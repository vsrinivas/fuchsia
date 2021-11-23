// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_ALGORITHM_H_
#define ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_ALGORITHM_H_

#include <lib/stdcompat/algorithm.h>

namespace ktl {

// "Non-modifying sequence operations"
using std::adjacent_find;
using std::all_of;
using std::any_of;
using std::count;
using std::count_if;
using std::find;
using std::find_end;
using std::find_first_of;
using std::find_if;
using std::find_if_not;
using std::for_each;
using std::for_each_n;
using std::none_of;
using std::search;
using std::search_n;

// "Modifying sequence operations" (subset)
using std::copy;
using std::copy_if;
using std::exchange;
using std::fill;
using std::fill_n;
using std::swap;
using std::transform;

// "Sorting operations" (subset)
using cpp20::is_sorted;
using cpp20::sort;
using std::is_sorted_until;
using std::stable_sort;

// "Binary search operations (on sorted ranges)"
using std::binary_search;
using std::equal_range;
using std::lower_bound;
using std::upper_bound;

// "Minimum/maximum operations"
using std::clamp;
using std::max;
using std::max_element;
using std::min;
using std::min_element;
using std::minmax;
using std::minmax_element;

}  // namespace ktl

#endif  // ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_ALGORITHM_H_
