// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/duration_predictor.h"

#include <lib/gtest/test_loop_fixture.h>

namespace scenic_impl {
namespace gfx {
namespace test {

TEST(DurationPredictor, FirstPredictionIsInitialPrediction) {
  const size_t kWindowSize = 4;
  const zx::duration kInitialPrediction = zx::usec(500);
  DurationPredictor predictor(kWindowSize, kInitialPrediction);
  EXPECT_EQ(predictor.GetPrediction(), kInitialPrediction);
}

TEST(DurationPredictor, PredictionAfterWindowFlushIsMeasurement) {
  const size_t kWindowSize = 4;
  const zx::duration kInitialPrediction = zx::msec(1);
  DurationPredictor predictor(kWindowSize, kInitialPrediction);

  const zx::duration measurement = zx::msec(5);
  EXPECT_GT(measurement, kInitialPrediction);
  predictor.InsertNewMeasurement(measurement);
  EXPECT_EQ(predictor.GetPrediction(), kInitialPrediction);

  for (size_t i = 0; i < kWindowSize - 1; ++i) {
    predictor.InsertNewMeasurement(measurement);
  }
  EXPECT_EQ(predictor.GetPrediction(), measurement);
}

TEST(DurationPredictor, PredictionIsSmallestInWindowAsMeasurementsIncrease) {
  size_t window_size = 10;
  DurationPredictor predictor(window_size, /* initial prediction */ zx::usec(0));

  for (size_t i = 1; i <= window_size; ++i) {
    predictor.InsertNewMeasurement(zx::msec(i));
  }
  EXPECT_EQ(predictor.GetPrediction(), zx::msec(1));
}

TEST(DurationPredictor, PredictionIsSmallestInWindowAsMeasurementsDecrease) {
  size_t window_size = 10;
  DurationPredictor predictor(window_size, /* initial prediction */ zx::usec(0));

  for (size_t i = window_size; i > 0; --i) {
    predictor.InsertNewMeasurement(zx::msec(i));
  }
  EXPECT_EQ(predictor.GetPrediction(), zx::msec(1));
}

TEST(DurationPredictor, PredictionIsSmallestInWindow) {
  size_t window_size = 10;
  DurationPredictor predictor(window_size, /* initial prediction */ zx::usec(0));

  const std::vector<zx_duration_t> measurements{12, 4, 5, 2, 8, 55, 13, 6, 8, 9};
  for (size_t i = 0; i < measurements.size(); ++i) {
    predictor.InsertNewMeasurement(zx::msec(measurements[i]));
  }
  EXPECT_EQ(predictor.GetPrediction(), zx::msec(2));
}

TEST(DurationPredictor, MinIsResetWhenSmallestIsOutOfWindow) {
  size_t window_size = 4;
  DurationPredictor predictor(window_size, /* initial prediction */ zx::usec(0));

  const std::vector<zx_duration_t> measurements{12, 4, 5, 2, 8, 55, 13, 6, 8, 9};
  for (size_t i = 0; i < measurements.size(); ++i) {
    predictor.InsertNewMeasurement(zx::msec(measurements[i]));
  }
  EXPECT_EQ(predictor.GetPrediction(), zx::msec(6));
}

TEST(DurationPredictor, WindowSizeOfOneWorks) {
  size_t window_size = 1;
  DurationPredictor predictor(window_size, /* initial prediction */ zx::usec(0));

  for (size_t i = 0; i < 5; ++i) {
    predictor.InsertNewMeasurement(zx::msec(i));
  }
  EXPECT_EQ(predictor.GetPrediction(), zx::msec(4));
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
