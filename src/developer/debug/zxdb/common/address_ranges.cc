// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/address_ranges.h"

#include <lib/syslog/cpp/macros.h>

#include <algorithm>

namespace zxdb {

AddressRanges::AddressRanges(Format format, RangeVector ranges) : ranges_(std::move(ranges)) {
  if (format == kCanonical) {
    FX_DCHECK(IsCanonical(ranges_));
  } else {
    // Non-canonical input. We still expect the common case to be canonical so
    // do a verification step before trying to sort.
    if (!IsCanonical(ranges_))
      Canonicalize();
  }
}

std::optional<AddressRange> AddressRanges::GetRangeContaining(uint64_t addr) const {
  if (empty())
    return std::nullopt;

  // This would be faster using brute-force for smallish numbers of elements,
  // but it doesn't matter that much and forcing the more complex code path in
  // all cases helps ensure correctness.
  auto found = std::upper_bound(ranges_.begin(), ranges_.end(), addr, AddressRangeEndAddrCmp());
  if (found == ranges_.end() || !found->InRange(addr))
    return std::nullopt;
  return *found;
}

bool AddressRanges::InRange(uint64_t addr) const { return !!GetRangeContaining(addr); }

AddressRange AddressRanges::GetExtent() const {
  if (empty())
    return AddressRange();
  return AddressRange(front().begin(), back().end());
}

std::string AddressRanges::ToString() const {
  std::string result("{");
  for (size_t i = 0; i < ranges_.size(); i++) {
    if (i > 0)
      result += ", ";
    result += ranges_[i].ToString();
  }
  result += '}';
  return result;
}

// static
bool AddressRanges::IsCanonical(const AddressRanges::RangeVector& ranges) {
  if (!ranges.empty() && ranges[0].empty())
    return false;  // First item is empty.

  // Check remaining items for both empty and non-sorted relative to previous.
  for (size_t i = 1; i < ranges.size(); i++) {
    if (ranges[i].empty() || ranges[i].begin() < ranges[i - 1].end())
      return false;
  }
  return true;
}

void AddressRanges::Canonicalize() {
  // Ensure sorted by the beginning address.
  std::sort(ranges_.begin(), ranges_.end(), AddressRangeBeginCmp());

  size_t i = 0;
  while (i < ranges_.size()) {
    if (ranges_[i].empty() || (i > 0 && ranges_[i - 1].Contains(ranges_[i]))) {
      // New range is unnecessary, delete it.
      ranges_.erase(ranges_.begin() + i);
    } else if (i > 0 && ranges_[i - 1].Overlaps(ranges_[i])) {
      // Extend the existing range to encompass this new one.
      ranges_[i - 1] = ranges_[i - 1].Union(ranges_[i]);
      ranges_.erase(ranges_.begin() + i);
    } else {
      i++;
    }
  }
}

}  // namespace zxdb
