// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/duration_predictor.h"

#include <src/lib/fxl/logging.h>

namespace scenic_impl {
namespace gfx {

DurationPredictor::DurationPredictor(size_t optimism_window_size,
                    zx::duration initial_prediction)
    : kWindowSize(optimism_window_size),
      window_(kWindowSize, initial_prediction) {
  FXL_DCHECK(kWindowSize > 0);
  current_minimum_duration_index_ = kWindowSize - 1;
}

zx::duration DurationPredictor::GetPrediction() const {
  return window_[current_minimum_duration_index_];
}

void DurationPredictor::InsertNewMeasurement(zx::duration duration) {
    // Move window forward.
    window_.push_front(duration);
    window_.pop_back();
    ++current_minimum_duration_index_;

    if (current_minimum_duration_index_ >= kWindowSize) {
      // If old min went out of scope, find the new min.
      current_minimum_duration_index_ = 0;
      for (size_t i = 1; i < kWindowSize; ++i) {
        if (window_[i] < window_[current_minimum_duration_index_]) {
          current_minimum_duration_index_ = i;
        }
      }
    } else if (window_.front() <= window_[current_minimum_duration_index_]) {
      // Use newest possible minimum.
      current_minimum_duration_index_ = 0;
    }
}

}  // namespace gfx
}  // namespace scenic_impl
