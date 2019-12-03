// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_COMMON_LARGEST_LESS_OR_EQUAL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_COMMON_LARGEST_LESS_OR_EQUAL_H_

#include <algorithm>

namespace zxdb {

// Returns an iterator pointing to the largest element in [first, last) less than or equal to the
// given |val|. As with std::lower_bound, the range [first, last) must be sorted according to
// the |less| comparator.
//
// For example, if you had a sorted range of addresses and you want to know which one begins the
// range which an address falls into:
//
//    std::vector<AddressRecord> ranges;
//    int64_t address;
//
//    auto found = LargestLessOrEqual(
//        ranges.begin(), ranges.end(), address,
//        [](const AddressRecord& record, uint64_t addr) { return record.addr < addr; },
//        [](const AddressRecord& record, uint64_t addr) { return record.addr == addr; });
//
// For simple types, you can pass |std::less<T>()| and |std::equal_to<T>()| from <functional> for
// the comparators.
template <class BidirectionalIterator, class T, class CompareLess, class CompareEqual>
BidirectionalIterator LargestLessOrEqual(BidirectionalIterator first, BidirectionalIterator last,
                                         const T& val, CompareLess less, CompareEqual equals) {
  if (first == last)
    return last;  // Nothing to return.

  auto lower_bound = std::lower_bound(first, last, val, less);

  if (lower_bound != last && equals(*lower_bound, val))
    return lower_bound;  // Got an exact match.

  // Otherwise, the result is the previous item in the range.
  if (lower_bound == first)
    return last;  // No previous item, |val| is before the range.
  return --lower_bound;
}

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_COMMON_LARGEST_LESS_OR_EQUAL_H_
