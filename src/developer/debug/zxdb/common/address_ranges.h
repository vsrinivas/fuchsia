// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "src/developer/debug/zxdb/common/address_range.h"

namespace zxdb {

// A set of address ranges. It exposes a read-only vector-like interface
// along with some helper functions to test for inclusion.
//
// The ranges in the structure are always stored in canonical form: sorted by
// the begin address, non-overlapping, no empty ranges.
class AddressRanges {
 public:
  using RangeVector = std::vector<AddressRange>;

  using const_iterator = RangeVector::const_iterator;
  using const_reverse_iterator = RangeVector::const_reverse_iterator;

  // Indicates the type of input. Canonical input is a set of sorted,
  // non-overlapping ranges. Non-canonical input can be anything. Non-canonical
  // input will be sorted and un-overlapped.
  enum Format { kCanonical, kNonCanonical };

  // Creates an empty set of ranges.
  AddressRanges() = default;

  AddressRanges(const AddressRanges& other) : ranges_(other.ranges_) {}
  AddressRanges(AddressRanges&& other) noexcept
      : ranges_(std::move(other.ranges_)) {}

  // Creates a single-range set. If the range is empty it will not be added.
  explicit AddressRanges(const AddressRange& r) {
    if (!r.empty())
      ranges_.push_back(r);
  }

  // Creates from a set of ranges.
  AddressRanges(Format, RangeVector ranges);

  AddressRanges& operator=(const AddressRanges& other) {
    ranges_ = other.ranges_;
    return *this;
  }
  AddressRanges& operator=(AddressRanges&& other) {
    ranges_ = std::move(other.ranges_);
    return *this;
  }

  size_t size() const noexcept { return ranges_.size(); }
  bool empty() const { return ranges_.empty(); }

  const AddressRange& operator[](size_t i) const { return ranges_[i]; }

  const AddressRange& front() const { return ranges_.front(); }
  const AddressRange& back() const { return ranges_.back(); }

  const_iterator begin() const noexcept { return ranges_.begin(); }
  const_iterator cbegin() const noexcept { return ranges_.cbegin(); }
  const_iterator end() const noexcept { return ranges_.end(); }
  const_iterator cend() const noexcept { return ranges_.cend(); }
  const_reverse_iterator rbegin() const noexcept { return ranges_.rbegin(); }
  const_reverse_iterator crbegin() const noexcept { return ranges_.crbegin(); }
  const_reverse_iterator rend() const noexcept { return ranges_.rend(); }
  const_reverse_iterator crend() const noexcept { return ranges_.crend(); }

  // Returns the individual subrange that includes the given address if one
  // exists.
  std::optional<AddressRange> GetRangeContaining(uint64_t addr) const;

  // Returns true if the address is included in any of the ranges.
  bool InRange(uint64_t addr) const;

  // Returns a string representing this set of ranges for debugging purposes.
  std::string ToString() const;

  // Returns true if the given vector is in canonical form.
  static bool IsCanonical(const AddressRanges::RangeVector& ranges);

 private:
  void Canonicalize();

  RangeVector ranges_;
};

}  // namespace zxdb
