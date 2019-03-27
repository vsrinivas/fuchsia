// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <string>

namespace zxdb {

class AddressRange {
 public:
  AddressRange() = default;
  AddressRange(uint64_t begin, uint64_t end);

  uint64_t begin() const { return begin_; }
  uint64_t end() const { return end_; }

  uint64_t size() const { return end_ - begin_; }
  bool empty() const { return end_ == begin_; }

  bool InRange(uint64_t addr) const { return addr >= begin_ && addr < end_; }

  bool Contains(const AddressRange& other) const {
    return other.begin_ >= begin_ && other.end_ <= end_;
  }
  bool Overlaps(const AddressRange& other) const {
    return other.begin_ < end_ && other.end_ >= begin_;
  }

  // Returns a new range covering both inputs (|this| and |other|). If the
  // inputs don't touch, the result will also cover the in-between addresses.
  // Use the AddressRanges class if you need to represent multiple
  // discontiguous ranges.
  [[nodiscard]] AddressRange Union(const AddressRange& other) const;

  bool operator==(const AddressRange& other) const {
    return begin_ == other.begin_ && end_ == other.end_;
  }
  bool operator!=(const AddressRange& other) const {
    return !operator==(other);
  }

  // Returns a string representing this set of ranges for debugging purposes.
  std::string ToString() const;

 private:
  uint64_t begin_ = 0;
  uint64_t end_ = 0;
};

// Comparison functor for comparing the beginnings of address ranges.
// Secondarily sorts based on size.
struct AddressRangeBeginCmp {
  bool operator()(const AddressRange& a, const AddressRange& b) const {
    if (a.begin() == b.begin())
      return a.size() < b.size();
    return a.begin() < b.begin();
  }
};

// Compares an address with the ending of a range. For searching for an address
// using lower_bound address in a sorted list of ranges. Using this comparator,
// lower_bound will find the element that contains the item if it exists.
struct AddressRangeEndAddrCmp {
  bool operator()(const AddressRange& range, uint64_t addr) const {
    return range.end() < addr;
  }
};

}  // namespace zxdb
