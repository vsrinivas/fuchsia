// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTING_LOADBENCH_MEASUREMENT_H_
#define SRC_TESTING_LOADBENCH_MEASUREMENT_H_

#include <lib/zx/time.h>

#include <algorithm>
#include <cstddef>
#include <limits>

class Measurement {
 public:
  Measurement() = default;
  Measurement(const Measurement&) = default;
  Measurement& operator=(const Measurement&) = default;

  void StartInterval(zx::time timestamp) {
    if (!interval_active_) {
      interval_active_ = true;
      interval_start_ = timestamp;
    }
  }
  void EndInterval(zx::time timestamp) {
    if (interval_active_) {
      interval_active_ = false;
      const auto interval = timestamp - interval_start_;

      sample_count_++;
      interval_accum_ += interval;
      interval_min_ = std::min(interval_min_, interval);
      interval_max_ = std::max(interval_max_, interval);
    }
  }

 private:
  bool interval_active_{false};
  zx::time interval_start_{0};

  zx::duration interval_min_{std::numeric_limits<zx_duration_t>::max()};
  zx::duration interval_max_{std::numeric_limits<zx_duration_t>::min()};

  zx::duration interval_accum_{0};
  size_t sample_count_{0};
};

#endif  // SRC_TESTING_LOADBENCH_MEASUREMENT_H_
