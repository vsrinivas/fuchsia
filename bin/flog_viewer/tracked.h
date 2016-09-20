// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_TOOLS_FLOG_VIEWER_TRACKED_H_
#define APPS_MEDIA_TOOLS_FLOG_VIEWER_TRACKED_H_

#include <limits>

namespace mojo {
namespace flog {

// Tracks a sequence of values that may be added or removed.
class Tracked {
public:
  // Returns the smallest value that was ever added.
  uint64_t min() const { return min_; }

  // Returns the last value that was added.
  uint64_t curr() const { return curr_; }

  // Returns the largest value that was ever added.
  uint64_t max() const { return max_; }

  // Returns the count of values added.
  size_t count() const { return count_; }

  // Returns the sum of all values added.
  uint64_t total() const { return total_; }

  // Returns the average of all values added.
  uint64_t average() const { return total_ / count_; }

  // Returns the count of values added minus the count of values removed.
  size_t outstanding_count() const { return outstanding_count_; }

  // Returns The sum of all values added minus the sum of all values removed.
  uint64_t outstanding_total() const { return outstanding_total_; }

  // Returns |outstanding_total| divided by |outstanding_count|.
  uint64_t outstanding_average() const {
    return outstanding_total_ / outstanding_count_;
  }

  // Returns the highest value attained by |outstanding_count|.
  size_t max_outstanding_count() const { return max_outstanding_count_; }

  // Returns the highest value attained by |outstanding_total|.
  uint64_t max_outstanding_total() const { return max_outstanding_total_; }

  // Adds a value.
  void Add(uint64_t t) {
    curr_ = t;

    if (min_ > t) {
      min_ = t;
    }

    if (max_ < t) {
      max_ = t;
    }

    ++count_;
    total_ += t;
    ++outstanding_count_;
    outstanding_total_ += t;

    if (max_outstanding_count_ < outstanding_count_) {
      max_outstanding_count_ = outstanding_count_;
    }

    if (max_outstanding_total_ < outstanding_total_) {
      max_outstanding_total_ = outstanding_total_;
    }
  }

  // Removes a value.
  void Remove(uint64_t t) {
    --outstanding_count_;
    outstanding_total_ -= t;
  }

private:
  uint64_t min_ = std::numeric_limits<uint64_t>::max();
  uint64_t curr_ = uint64_t();
  uint64_t max_ = std::numeric_limits<uint64_t>::min();
  size_t count_ = 0;
  uint64_t total_ = uint64_t();
  size_t outstanding_count_ = 0;
  uint64_t outstanding_total_ = uint64_t();
  size_t max_outstanding_count_ = 0;
  uint64_t max_outstanding_total_ = std::numeric_limits<uint64_t>::min();
};

} // namespace flog
} // namespace mojo

#endif // APPS_MEDIA_TOOLS_FLOG_VIEWER_TRACKED_H_
