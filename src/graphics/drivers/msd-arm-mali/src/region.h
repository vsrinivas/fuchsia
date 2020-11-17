// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGION_H
#define REGION_H

#include <array>
#include <cstdint>
#include <utility>

class Region {
 public:
  Region() = default;
  static Region FromStartAndLength(uint64_t start, uint64_t length) {
    return Region(start, length + start);
  }

  static Region FromStartAndEnd(uint64_t start, uint64_t end) { return Region(start, end); }

  void set_start(uint64_t start) { start_ = start; }
  void set_end(uint64_t end) { end_ = end; }

  uint64_t start() const { return start_; }
  uint64_t end() const { return end_; }

  bool empty() const { return length() == 0; }
  uint64_t length() const { return end_ - start_; }
  // In-place. Modifies this region to include both regions (and the gap between them if necessary).
  void Union(const Region& other);

  // Subtract and possibly split into two separate regions. The lower region is output at index 0.
  std::array<Region, 2> SubtractWithSplit(const Region& other) const;

  // In-place subtraction. Returns false if subtraction would need to split into two regions.
  bool Subtract(const Region& other);
  // Returns true if this region contains |other|. This is also true if both regions are empty.
  bool Contains(const Region& other);

  void Intersect(const Region& other);

  // Returns false if either region is empty.
  bool IsAdjacentTo(const Region& other);

  bool operator==(const Region& other) const {
    if (empty() && other.empty())
      return true;

    return start_ == other.start_ && end_ == other.end_;
  }

 private:
  Region(uint64_t start, uint64_t end) : start_(start), end_(end) {}

  uint64_t start_{};
  uint64_t end_{};  // Non-inclusive.
};

#endif  // SRC_GRAPHICS_DRIVERS_MSD_ARM_MALI_SRC_REGION_H_
