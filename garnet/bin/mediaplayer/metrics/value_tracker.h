// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_METRICS_VALUE_TRACKER_H_
#define GARNET_BIN_MEDIAPLAYER_METRICS_VALUE_TRACKER_H_

#include <algorithm>
#include <limits>

namespace media_player {

// Tracks a value over time.
template <typename T>
class ValueTracker {
 public:
  ValueTracker() { Reset(); }

  ~ValueTracker() {}

  // Adds a sample to the tracker.
  void AddSample(T value) {
    ++count_;
    sum_ += value;
    min_ = std::min(min_, value);
    max_ = std::max(max_, value);
  }

  // Resets the tracker to its initial state.
  void Reset() {
    count_ = 0;
    sum_ = 0;
    min_ = std::numeric_limits<T>::max();
    max_ = std::numeric_limits<T>::min();
  }

  // Sample count.
  int64_t count() const { return count_; }

  // Sum of all samples.
  T sum() const { return sum_; }

  // Minimum of all samples.
  T min() const { return min_; }

  // Average of all samples.
  T average() const { return sum_ / count_; }

  // Maximum of all samples.
  T max() const { return max_; }

 private:
  int64_t count_;
  T sum_;
  T min_;
  T max_;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_METRICS_VALUE_TRACKER_H_
