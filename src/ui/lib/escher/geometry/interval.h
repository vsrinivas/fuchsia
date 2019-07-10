// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_GEOMETRY_INTERVAL_H_
#define SRC_UI_LIB_ESCHER_GEOMETRY_INTERVAL_H_

#include <vector>

#include "src/ui/lib/escher/geometry/types.h"

namespace escher {

// This class represents an interval on the real number line. The intervals
// represented are closed (i.e. they contain the endpoints).
class Interval {
 public:
  // Non-empty interval.  No error-checking; it is up to the caller to ensure that
  // all components of max are >= the corresponding component of min.
  Interval(float min, float max);

  // Empty Interval - Set max smaller than min.
  constexpr Interval() : min_(1), max_(0) {}

  float min() const { return min_; }
  float max() const { return max_; }

  bool operator==(const Interval& interval) const {
    return min_ == interval.min_ && max_ == interval.max_;
  }
  bool operator!=(const Interval& interval) const { return !(*this == interval); }

  bool is_empty() const { return *this == Interval(); }

  // Expand this interval to encompass the other. Return a new interval.
  Interval Join(const Interval& interval);

  // Shrink this interval to be the intersection of this with the other.  If the
  // intervals do not intersect, this interval becomes empty.  Return a new interval.
  Interval Intersect(const Interval& interval) const;

  float length() const {
    FXL_DCHECK(!is_empty());
    return max_ - min_;
  }

  // Return true if the other interval is completely contained by this one.
  bool Contains(const Interval& interval) const {
    return interval.min_ >= min_ && interval.max_ <= max_;
  }

  bool Contains(const float& t) const { return min_ <= t && t <= max_; }

 private:
  float min_;
  float max_;
};

// Debugging.
ESCHER_DEBUG_PRINTABLE(Interval);

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_GEOMETRY_INTERVAL_H_
