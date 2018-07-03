// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_UTIL_RATE_LIMITED_RETRY_H_
#define PERIDOT_LIB_UTIL_RATE_LIMITED_RETRY_H_

#include <lib/fxl/time/time_delta.h>
#include <lib/fxl/time/time_point.h>

namespace modular {

// Keeps track of a retry scheme where infinite retries are allowed unless an
// operation fails many times in a short interval. This can be used to enable a
// decent user experience in the face of a flaky dependency without undue churn
// or log spamming if an unrecoverable failure has occurred.
class RateLimitedRetry {
 public:
  struct Threshold {
    unsigned int count;
    fxl::TimeDelta period;
  };

  // Constructs a retry tracker where retry should occur as long as no more than
  // |count| failures have occurred within a |period|.
  //
  // As an example, an allowance of 1 failure per second will allow retries if
  // failures occur no more frequently than exactly once every second.
  RateLimitedRetry(const Threshold& threshold);

  // Call |ShouldRetry()| when the operation you are tracking fails, to
  // determine whether a retry should be attempted. Returns |false| if
  // |ShouldRetry()| has been called more than |count| times within a |period|.
  bool ShouldRetry();

 private:
  const Threshold threshold_;

  unsigned int failure_series_count_;
  fxl::TimePoint failure_series_start_;
};

}  // namespace modular

#endif  // PERIDOT_LIB_UTIL_RATE_LIMITED_RETRY_H_
