// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_ADDRESS_RANGE_H_
#define SRC_DEVELOPER_DEBUG_SHARED_ADDRESS_RANGE_H_

#include <lib/syslog/cpp/macros.h>
#include <stdint.h>

#include <string>

#include "src/developer/debug/shared/serialization.h"

namespace debug {

// Represents a range of addresses with common operations.
//
// Since end() is one-past-the-end, we technically can't represent the last byte in the address
// space. It might be better to change this class to be (begin, size) to avoid this problem but
// that's a difficult change.
class AddressRange {
 public:
  constexpr AddressRange() = default;
  constexpr AddressRange(uint64_t begin, uint64_t end) : begin_(begin), end_(end) {
    FX_DCHECK(end_ >= begin_);
  }

  // Returns an address range covering the entire address space. Since our end() is one-past-the
  // end, it won't technically cover the last byte.
  static constexpr AddressRange Everything() {
    return AddressRange(0, std::numeric_limits<uint64_t>::max());
  }

  constexpr uint64_t begin() const { return begin_; }
  constexpr uint64_t end() const { return end_; }

  constexpr uint64_t size() const { return end_ - begin_; }
  constexpr bool empty() const { return end_ == begin_; }

  constexpr bool InRange(uint64_t addr) const { return addr >= begin_ && addr < end_; }

  // Callers need to consider the semantics they want for empty ranges.
  //
  // An empty range whose start and end are within this range is considered to Contain/Overlap
  // this one. If you want to consider empty ranges as being unoverlapping with anything you will
  // need to perform an extra check.
  constexpr bool Contains(const AddressRange& other) const {
    return other.begin_ >= begin_ && other.end_ <= end_;
  }
  constexpr bool Overlaps(const AddressRange& other) const {
    return other.begin_ < end_ && other.end_ >= begin_;
  }

  // Returns a new range covering both inputs (|this| and |other|). If the inputs don't touch, the
  // result will also cover the in-between addresses. Use the AddressRanges class if you need to
  // represent multiple discontiguous ranges. Empty ranges do not count toward a union.
  [[nodiscard]] AddressRange Union(const AddressRange& other) const;

  constexpr bool operator==(const AddressRange& other) const {
    return begin_ == other.begin_ && end_ == other.end_;
  }
  constexpr bool operator!=(const AddressRange& other) const { return !operator==(other); }

  // Returns a string representing this set of ranges for debugging purposes.
  std::string ToString() const;

  void Serialize(Serializer& ser, uint32_t ver) { ser | begin_ | end_; }

 private:
  uint64_t begin_ = 0;
  uint64_t end_ = 0;
};

// Comparison functor for comparing the beginnings of address ranges. Secondarily sorts based on
// size.
struct AddressRangeBeginCmp {
  bool operator()(const AddressRange& a, const AddressRange& b) const {
    if (a.begin() == b.begin())
      return a.size() < b.size();
    return a.begin() < b.begin();
  }
};

// Compares an address with the ending of a range. For searching for an address using lower_bound
// address in a sorted list of ranges. Using this comparator, lower_bound will find the element that
// contains the item if it exists.
struct AddressRangeEndAddrCmp {
  bool operator()(const AddressRange& range, uint64_t addr) const { return range.end() < addr; }
  bool operator()(uint64_t addr, const AddressRange& range) const { return addr < range.end(); }
};

// Used for putting address ranges in a set where range uniqueness is required.
struct AddressRangeEqualityCmp {
  bool operator()(const AddressRange& a, const AddressRange& b) const {
    if (a.begin() == b.begin())
      return a.end() < b.end();
    return a.begin() < b.begin();
  }
};

}  // namespace debug

#endif  // SRC_DEVELOPER_DEBUG_SHARED_ADDRESS_RANGE_H_
