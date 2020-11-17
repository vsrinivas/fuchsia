// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "region.h"

#include "magma_util/macros.h"

void Region::Union(const Region& other) {
  if (other.empty()) {
    return;
  }
  if (empty()) {
    *this = other;
    return;
  }
  start_ = std::min(start_, other.start_);
  end_ = std::max(end_, other.end_);
}

std::array<Region, 2> Region::SubtractWithSplit(const Region& other) const {
  if (empty() || other.empty()) {
    return {*this, {}};
  }
  Region old_region(*this);
  if (other.start_ >= end_) {
    // Other is after this region.
    return {*this, {}};
  }
  if (other.end_ >= end_) {
    // Other contains end of this region. Remove end of range.
    uint64_t new_end_ = other.start_;
    if (new_end_ <= start_) {
      // Region is now empty.
      new_end_ = start_;
    }
    Region r = {start_, new_end_};
    DASSERT(old_region.Contains(r));
    return {r, {}};
  }

  if (other.start_ > start_) {
    // Other is contained completely within this region. Split into two.
    Region left_region = Region{start_, other.start_};
    Region other_region = Region{other.end_, end_};
    return {left_region, other_region};
  }
  if (other.end_ <= start_) {
    // Other is before this region, so regions don't intersect at all.
    return {*this, {}};
  }
  // other contains the leftmost part of this region, but not the rightmost part.
  uint64_t new_start_ = std::max(other.end_, start_);
  Region r{new_start_, end_};

  DASSERT(old_region.Contains(r));
  return {r, {}};
}

bool Region::Subtract(const Region& other) {
  auto [new_left, new_right] = SubtractWithSplit(other);
  if (!new_left.empty() && !new_right.empty())
    return false;
  DASSERT(new_right.empty());
  *this = new_left;
  return true;
}

bool Region::Contains(const Region& other) {
  if (other.empty())
    return true;
  return (other.start_ >= start_ && other.end_ <= end_);
}

void Region::Intersect(const Region& other) {
  start_ = std::max(start_, other.start_);
  end_ = std::min(end_, other.end_);
  if (start_ > end_)
    start_ = end_;
}

bool Region::IsAdjacentTo(const Region& other) {
  if (empty() || other.empty())
    return false;
  return end_ == other.start_ || start_ == other.end_;
}
