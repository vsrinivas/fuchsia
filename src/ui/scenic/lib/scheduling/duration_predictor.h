// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCHEDULING_DURATION_PREDICTOR_H_
#define SRC_UI_SCENIC_LIB_SCHEDULING_DURATION_PREDICTOR_H_

#include <lib/zx/time.h>

#include <deque>

namespace scheduling {

// Class for predicting future durations based on previous measurements. Uses an
// pessmistic approach that determines the "most pessmistic duration" based on
// the last N measurements, where N is a range of values set by the client.
//
// TODO(fxbug.dev/24606) When Scenic has priority gpu vk queues, revisit this
// prediction strategy. Scenic currently cannot report accurate GPU duration
// measurements because it currently has no way to preempt work on the GPU.
// This causes render durations to be very noisy and not representative of the
// work Scenic is doing.
class DurationPredictor {
 public:
  DurationPredictor(size_t window_size, zx::duration initial_prediction);
  ~DurationPredictor() = default;

  zx::duration GetPrediction() const;

  void InsertNewMeasurement(zx::duration duration);

 private:
  const size_t kWindowSize;
  std::deque<zx::duration> window_;  // Ring buffer.

  size_t current_maximum_duration_index_ = 0;
};

}  // namespace scheduling

#endif  // SRC_UI_SCENIC_LIB_SCHEDULING_DURATION_PREDICTOR_H_
