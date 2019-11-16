// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scheduling/duration_predictor.h"

#include <lib/gtest/test_loop_fixture.h>

namespace scheduling {
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

  for (size_t i = 0; i < kWindowSize - 1; ++i) {
    predictor.InsertNewMeasurement(measurement);
  }
  EXPECT_EQ(predictor.GetPrediction(), measurement);
}

TEST(DurationPredictor, PredictionIsLargestInWindowAsMeasurementsIncrease) {
  size_t window_size = 10;
  DurationPredictor predictor(window_size, /* initial prediction */ zx::usec(0));

  for (size_t i = 1; i <= window_size; ++i) {
    predictor.InsertNewMeasurement(zx::msec(i));
    EXPECT_EQ(predictor.GetPrediction(), zx::msec(i));
  }
}

TEST(DurationPredictor, PredictionIsLargestInWindowAsMeasurementsDecrease) {
  size_t window_size = 10;
  DurationPredictor predictor(window_size, /* initial prediction */ zx::usec(0));

  for (size_t i = window_size; i > 0; --i) {
    predictor.InsertNewMeasurement(zx::msec(i));
    EXPECT_EQ(predictor.GetPrediction(), zx::msec(window_size));
  }
}

TEST(DurationPredictor, PredictionIsLargestInWindow) {
  size_t window_size = 10;
  DurationPredictor predictor(window_size, /* initial prediction */ zx::usec(0));

  const std::vector<zx::duration> measurements{
      zx::msec(12), zx::msec(4),  zx::msec(5), zx::msec(2), zx::msec(8),
      zx::msec(15), zx::msec(13), zx::msec(6), zx::msec(8), zx::msec(9)};
  for (const auto& m : measurements) {
    predictor.InsertNewMeasurement(m);
  }
  EXPECT_EQ(predictor.GetPrediction(), zx::msec(15));
}

TEST(DurationPredictor, MaxIsResetWhenLargestIsOutOfWindow) {
  size_t window_size = 4;
  DurationPredictor predictor(window_size, /* initial prediction */ zx::usec(0));

  const std::vector<zx::duration> measurements{
      zx::msec(12), zx::msec(4),  zx::msec(5), zx::msec(2), zx::msec(8),
      zx::msec(55), zx::msec(13), zx::msec(6), zx::msec(8), zx::msec(9)};
  for (const auto& m : measurements) {
    predictor.InsertNewMeasurement(m);
  }
  EXPECT_EQ(predictor.GetPrediction(), zx::msec(13));
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
}  // namespace scheduling
