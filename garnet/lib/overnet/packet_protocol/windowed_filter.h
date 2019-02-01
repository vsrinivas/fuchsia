// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// Implementation based on QUIC implementation, comment from there:

// Implements Kathleen Nichols' algorithm for tracking the minimum (or maximum)
// estimate of a stream of samples over some fixed time interval. (E.g.,
// the minimum RTT over the past five minutes.) The algorithm keeps track of
// the best, second best, and third best min (or max) estimates, maintaining an
// invariant that the measurement time of the n'th best >= n-1'th best.

// The algorithm works as follows. On a reset, all three estimates are set to
// the same sample. The second best estimate is then recorded in the second
// quarter of the window, and a third best estimate is recorded in the second
// half of the window, bounding the worst case error when the true min is
// monotonically increasing (or true max is monotonically decreasing) over the
// window.
//
// A new best sample replaces all three estimates, since the new best is lower
// (or higher) than everything else in the window and it is the most recent.
// The window thus effectively gets reset on every new min. The same property
// holds true for second best and third best estimates. Specifically, when a
// sample arrives that is better than the second best but not better than the
// best, it replaces the second and third best estimates but not the best
// estimate. Similarly, a sample that is better than the third best estimate
// but not the other estimates replaces only the third best estimate.
//
// Finally, when the best expires, it is replaced by the second best, which in
// turn is replaced by the third best. The newest sample replaces the third
// best.

namespace overnet {

template <class T>
class MinFilter {
 public:
  static bool Compare(T a, T b) { return a <= b; }
};

template <class T>
class MaxFilter {
 public:
  static bool Compare(T a, T b) { return a >= b; }
};

template <class Time, class Value, template <typename> class Compare>
class WindowedFilter {
  static Time some_time();

 public:
  using DeltaTime = decltype(some_time() - some_time());

  WindowedFilter(DeltaTime window_length, Time zero_time, Value zero_value)
      : window_length_(window_length),
        zero_time_(zero_time),
        zero_value_(zero_value),
        estimates_{{zero_time, zero_value},
                   {zero_time, zero_value},
                   {zero_time, zero_value}} {}

  void Update(Time time, Value value) {
    // Reset all estimates if they have not yet been initialized, if new sample
    // is a new best, or if the newest recorded estimate is too old.
    if (estimates_[0].value == zero_value_ ||
        Compare<Value>::Compare(value, estimates_[0].value) ||
        time - estimates_[2].time > window_length_) {
      Reset(time, value);
      return;
    }
    if (Compare<Value>::Compare(value, estimates_[1].value)) {
      estimates_[1] = Sample{time, value};
      estimates_[2] = estimates_[1];
    } else if (Compare<Value>::Compare(value, estimates_[2].value)) {
      estimates_[2] = Sample{time, value};
    }
    // Expire and update estimates as necessary.
    if (time - estimates_[0].time > window_length_) {
      // The best estimate hasn't been updated for an entire window, so promote
      // second and third best estimates.
      estimates_[0] = estimates_[1];
      estimates_[1] = estimates_[2];
      estimates_[2] = Sample{time, value};
      // Need to iterate one more time. Check if the new best estimate is
      // outside the window as well, since it may also have been recorded a
      // long time ago. Don't need to iterate once more since we cover that
      // case at the beginning of the method.
      if (time - estimates_[0].time > window_length_) {
        estimates_[0] = estimates_[1];
        estimates_[1] = estimates_[2];
      }
      return;
    }
    if (estimates_[1].value == estimates_[0].value &&
        time - estimates_[1].time > window_length_ / 4) {
      // A quarter of the window has passed without a better sample, so the
      // second-best estimate is taken from the second quarter of the window.
      estimates_[2] = estimates_[1] = Sample{time, value};
      return;
    }
    if (estimates_[2].value == estimates_[1].value &&
        time - estimates_[2].time > window_length_ / 2) {
      // We've passed a half of the window without a better estimate, so take
      // a third-best estimate from the second half of the window.
      estimates_[2] = Sample{time, value};
    }
  }

  // Resets all estimates to new value.
  void Reset(Time time, Value value) {
    estimates_[0] = estimates_[1] = estimates_[2] = Sample{time, value};
  }

  Value best_estimate() const { return estimates_[0].value; }
  Value second_best_estimate() const { return estimates_[1].value; }
  Value third_best_estimate() const { return estimates_[2].value; }

 private:
  struct Sample {
    Time time;
    Value value;
  };
  const DeltaTime window_length_;
  const Time zero_time_;
  const Value zero_value_;
  Sample estimates_[3];
};

}  // namespace overnet
