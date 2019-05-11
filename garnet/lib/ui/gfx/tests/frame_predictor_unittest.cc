// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/frame_predictor.h"

#include <lib/gtest/test_loop_fixture.h>

#include "garnet/lib/ui/gfx/tests/error_reporting_test.h"

namespace scenic_impl {
namespace gfx {
namespace test {

class FramePredictorTest : public ErrorReportingTest {
 protected:
  // | ::testing::Test |
  void SetUp() override {
    predictor_ = std::make_unique<FramePredictor>(kInitialRenderTimePrediction,
                                                  kInitialUpdateTimePrediction);
  }
  // | ::testing::Test |
  void TearDown() override { predictor_.reset(); }

  zx::time ms_to_time(uint64_t ms) {
    return zx::time(0) + zx::msec(ms);
  }

  static constexpr zx::duration kInitialRenderTimePrediction =
      zx::msec(4);
  static constexpr zx::duration kInitialUpdateTimePrediction =
      zx::msec(2);

  std::unique_ptr<FramePredictor> predictor_;
};

TEST_F(FramePredictorTest, BasicPredictions_ShouldBeReasonable) {
  PredictionRequest request = {
      .now = ms_to_time(5),
      .requested_presentation_time = ms_to_time(10),
      .last_vsync_time = ms_to_time(0),
      .vsync_interval = zx::msec(10)};

  auto prediction = predictor_->GetPrediction(request);

  EXPECT_GT(prediction.presentation_time, request.now);
  EXPECT_GE(prediction.latch_point_time, request.now);
  EXPECT_LT(prediction.latch_point_time, prediction.presentation_time);
}

TEST_F(FramePredictorTest, PredictionsAfterUpdating_ShouldBeMoreReasonable) {
  const zx::duration update_duration = zx::msec(2);
  const zx::duration render_duration = zx::msec(5);

  const size_t kBiggerThanAllPredictionWindows = 5;
  for (size_t i = 0; i < kBiggerThanAllPredictionWindows; ++i) {
    predictor_->ReportRenderDuration(render_duration);
    predictor_->ReportUpdateDuration(update_duration);
  }

  PredictionRequest request = {.now = ms_to_time(5),
                               .requested_presentation_time = ms_to_time(0),
                               .last_vsync_time = ms_to_time(0),
                               .vsync_interval = zx::msec(10)};

  auto prediction = predictor_->GetPrediction(request);

  EXPECT_GT(prediction.presentation_time, request.now);
  EXPECT_GE(prediction.latch_point_time, request.now);

  EXPECT_GE(prediction.presentation_time - prediction.latch_point_time,
            update_duration + render_duration);
}

TEST_F(FramePredictorTest,
       OneExpensiveTime_ShouldNotPredictForFutureVsyncIntervals) {
  const zx::duration update_duration = zx::msec(4);
  const zx::duration render_duration = zx::msec(10);
  const zx::duration vsync_interval = zx::msec(10);

  predictor_->ReportRenderDuration(render_duration);
  predictor_->ReportUpdateDuration(update_duration);

  PredictionRequest request = {.now = ms_to_time(0),
                               .requested_presentation_time = ms_to_time(0),
                               .last_vsync_time = ms_to_time(0),
                               .vsync_interval = vsync_interval};
  auto prediction = predictor_->GetPrediction(request);

  EXPECT_GE(prediction.latch_point_time, request.now);
  EXPECT_LE(prediction.presentation_time,
            request.last_vsync_time + request.vsync_interval);
}

TEST_F(FramePredictorTest,
       ManyExpensiveTimes_ShouldPredictForFutureVsyncIntervals) {
  const zx::duration update_duration = zx::msec(4);
  const zx::duration render_duration = zx::msec(10);
  const zx::duration vsync_interval = zx::msec(10);

  for (size_t i = 0; i < 10; i++) {
    predictor_->ReportRenderDuration(render_duration);
    predictor_->ReportUpdateDuration(update_duration);
  }

  PredictionRequest request = {.now = ms_to_time(3),
                               .requested_presentation_time = ms_to_time(0),
                               .last_vsync_time = ms_to_time(0),
                               .vsync_interval = vsync_interval};
  auto prediction = predictor_->GetPrediction(request);

  EXPECT_GE(prediction.latch_point_time, request.now);
  EXPECT_GE(prediction.presentation_time,
            request.last_vsync_time + request.vsync_interval);
  EXPECT_LE(prediction.presentation_time,
            request.last_vsync_time + request.vsync_interval * 2);
  EXPECT_LE(prediction.latch_point_time,
            prediction.presentation_time - request.vsync_interval);
}

TEST_F(FramePredictorTest, ManyFramesOfPredictions_ShouldBeReasonable) {
  const zx::duration vsync_interval = zx::msec(10);

  zx::time now = ms_to_time(0);
  zx::time requested_present = ms_to_time(8);
  zx::time last_vsync_time = ms_to_time(0);
  for (uint64_t i = 0; i < 50; ++i) {
    zx::duration update_duration = zx::msec(i % 5);
    zx::duration render_duration = zx::msec(5);
    predictor_->ReportUpdateDuration(update_duration);
    predictor_->ReportRenderDuration(render_duration);
    EXPECT_GE(vsync_interval, update_duration + render_duration);

    PredictionRequest request = {.now = now,
                               .requested_presentation_time = requested_present,
                               .last_vsync_time = last_vsync_time,
                               .vsync_interval = vsync_interval};
    auto prediction = predictor_->GetPrediction(request);

    EXPECT_GE(prediction.latch_point_time, request.now);
    EXPECT_GE(prediction.presentation_time, requested_present);
    EXPECT_LE(prediction.presentation_time,
              requested_present + vsync_interval * 2);

    // For the next frame, increase time to be after the predicted present to
    // emulate a client that is regularly scheduling frames.
    now = prediction.presentation_time + zx::msec(1);
    requested_present = prediction.presentation_time + vsync_interval;
    last_vsync_time = prediction.presentation_time;
  }
}

TEST_F(FramePredictorTest, MissedLastVsync_ShouldPredictWithInterval) {
  const zx::duration update_duration = zx::msec(4);
  const zx::duration render_duration = zx::msec(5);
  predictor_->ReportRenderDuration(render_duration);
  predictor_->ReportUpdateDuration(update_duration);

  const zx::duration vsync_interval = zx::msec(16);
  zx::time last_vsync_time = ms_to_time(16);
  // Make now be more than a vsync_interval beyond the last_vsync_time
  zx::time now = last_vsync_time + (vsync_interval * 2) + zx::msec(3);
  zx::time requested_present = now + zx::msec(9);
  PredictionRequest request = {.now = now,
                               .requested_presentation_time = requested_present,
                               .last_vsync_time = last_vsync_time,
                               .vsync_interval = vsync_interval};
  auto prediction = predictor_->GetPrediction(request);

  // The predicted presentation and wakeup times should be greater than one
  // vsync interval since the last reported vsync time.
  EXPECT_GE(prediction.presentation_time, last_vsync_time + vsync_interval);
  EXPECT_LE(prediction.presentation_time, now + (request.vsync_interval * 2));
  EXPECT_LE(prediction.presentation_time - prediction.latch_point_time,
            vsync_interval);
}

TEST_F(FramePredictorTest, MissedPresentRequest_ShouldTargetNextVsync) {
  const zx::duration update_duration = zx::msec(2);
  const zx::duration render_duration = zx::msec(4);
  predictor_->ReportRenderDuration(render_duration);
  predictor_->ReportUpdateDuration(update_duration);

  const zx::duration vsync_interval = zx::msec(10);
  zx::time last_vsync_time = ms_to_time(10);
  zx::time now = ms_to_time(12);
  // Request a present time in the past.
  zx::time requested_present = now - zx::msec(1);
  PredictionRequest request = {.now = now,
                               .requested_presentation_time = requested_present,
                               .last_vsync_time = last_vsync_time,
                               .vsync_interval = vsync_interval};
  auto prediction = predictor_->GetPrediction(request);

  EXPECT_GE(prediction.presentation_time, last_vsync_time + vsync_interval);
  EXPECT_LE(prediction.presentation_time, last_vsync_time + (vsync_interval * 2));
  EXPECT_GE(prediction.latch_point_time,
            prediction.presentation_time - vsync_interval);
}

TEST_F(FramePredictorTest, AttemptsToBeLowLatent_ShouldBePossible) {
  const zx::duration update_duration = zx::msec(1);
  const zx::duration render_duration = zx::msec(3);
  predictor_->ReportRenderDuration(render_duration);
  predictor_->ReportUpdateDuration(update_duration);

  const zx::duration vsync_interval = zx::msec(10);
  zx::time last_vsync_time = ms_to_time(10);
  zx::time requested_present = last_vsync_time + vsync_interval;
  zx::time now =
      requested_present - update_duration - render_duration - zx::msec(1);
  EXPECT_GT(now, last_vsync_time);

  PredictionRequest request = {.now = now,
                               .requested_presentation_time = requested_present,
                               .last_vsync_time = last_vsync_time,
                               .vsync_interval = vsync_interval};
  auto prediction = predictor_->GetPrediction(request);

  // The prediction should be for the next vsync.
  EXPECT_LE(prediction.presentation_time, last_vsync_time + vsync_interval);
  EXPECT_GE(prediction.latch_point_time, now);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
