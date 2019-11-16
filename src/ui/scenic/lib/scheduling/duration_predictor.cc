// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scheduling/duration_predictor.h"

#include "src/lib/fxl/logging.h"

namespace scheduling {

DurationPredictor::DurationPredictor(size_t window_size, zx::duration initial_prediction)
    : kWindowSize(window_size), window_(kWindowSize, initial_prediction) {
  FXL_DCHECK(kWindowSize > 0);
  current_maximum_duration_index_ = kWindowSize - 1;
}

zx::duration DurationPredictor::GetPrediction() const {
  return window_[current_maximum_duration_index_];
}

void DurationPredictor::InsertNewMeasurement(zx::duration duration) {
  // Move window forward.
  window_.push_front(duration);
  window_.pop_back();
  ++current_maximum_duration_index_;

  if (current_maximum_duration_index_ >= kWindowSize) {
    // If old min went out of scope, find the new min.
    current_maximum_duration_index_ = 0;
    for (size_t i = 1; i < kWindowSize; ++i) {
      if (window_[i] > window_[current_maximum_duration_index_]) {
        current_maximum_duration_index_ = i;
      }
    }
  } else if (window_.front() >= window_[current_maximum_duration_index_]) {
    // Use newest possible maximum.
    current_maximum_duration_index_ = 0;
  }
}

}  // namespace scheduling
