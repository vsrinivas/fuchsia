// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scheduling/frame_predictor.h"

#include <lib/gtest/test_loop_fixture.h>

#include "src/ui/scenic/lib/scheduling/constant_frame_predictor.h"
#include "src/ui/scenic/lib/scheduling/windowed_frame_predictor.h"

namespace scheduling {
namespace test {

namespace {
// Convenience helper to to convert zx::msec(duration) to a zx::time value.
zx::time ms_to_time(uint64_t ms) { return zx::time(0) + zx::msec(ms); }
}  // anonymous namespace

// ---------------------------------------------------------------------------
// WindowedFramePredictor tests
// ---------------------------------------------------------------------------
class WindowedFramePredictorTest : public ::gtest::TestLoopFixture {
 protected:
  // | ::testing::Test |
  void SetUp() override {
    predictor_ = std::make_unique<WindowedFramePredictor>(
        kMinPredictedFrameDuration, kInitialRenderTimePrediction, kInitialUpdateTimePrediction);
  }
  // | ::testing::Test |
  void TearDown() override { predictor_.reset(); }

  static constexpr zx::duration kMinPredictedFrameDuration = zx::msec(0);
  static constexpr zx::duration kInitialRenderTimePrediction = zx::msec(4);
  static constexpr zx::duration kInitialUpdateTimePrediction = zx::msec(2);

  std::unique_ptr<FramePredictor> predictor_;
};

TEST_F(WindowedFramePredictorTest, BasicPredictions_ShouldBeReasonable) {
  PredictionRequest request = {.now = ms_to_time(5),
                               .requested_presentation_time = ms_to_time(10),
                               .last_vsync_time = ms_to_time(0),
                               .vsync_interval = zx::msec(10)};

  auto prediction = predictor_->GetPrediction(request);

  EXPECT_GT(prediction.presentation_time, request.now);
  EXPECT_GE(prediction.latch_point_time, request.now);
  EXPECT_LT(prediction.latch_point_time, prediction.presentation_time);
}

TEST_F(WindowedFramePredictorTest, PredictionsAfterUpdating_ShouldBeMoreReasonable) {
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

TEST_F(WindowedFramePredictorTest, OneExpensiveTime_ShouldNotPredictForFutureVsyncIntervals) {
  const zx::duration update_duration = zx::msec(4);
  const zx::duration render_duration = zx::msec(30);

  const zx::duration vsync_interval = zx::msec(20);

  EXPECT_GT(render_duration + update_duration, vsync_interval);

  predictor_->ReportRenderDuration(render_duration);
  predictor_->ReportUpdateDuration(update_duration);

  PredictionRequest request = {.now = ms_to_time(0),
                               .requested_presentation_time = ms_to_time(0),
                               .last_vsync_time = ms_to_time(0),
                               .vsync_interval = vsync_interval};
  auto prediction = predictor_->GetPrediction(request);

  EXPECT_GE(prediction.latch_point_time, request.now);
  EXPECT_LE(prediction.presentation_time.get(),
            request.last_vsync_time.get() + vsync_interval.get());
}

TEST_F(WindowedFramePredictorTest, ManyExpensiveTimes_ShouldPredictForFutureVsyncIntervals) {
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
  EXPECT_GE(prediction.presentation_time, request.last_vsync_time + request.vsync_interval);
  EXPECT_LE(prediction.presentation_time, request.last_vsync_time + request.vsync_interval * 2);
  EXPECT_LE(prediction.latch_point_time.get(),
            prediction.presentation_time.get() - request.vsync_interval.get());
}

TEST_F(WindowedFramePredictorTest, ManyFramesOfPredictions_ShouldBeReasonable) {
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
    EXPECT_LE(prediction.presentation_time, requested_present + vsync_interval * 2);

    // For the next frame, increase time to be after the predicted present to
    // emulate a client that is regularly scheduling frames.
    now = prediction.presentation_time + zx::msec(1);
    requested_present = prediction.presentation_time + vsync_interval;
    last_vsync_time = prediction.presentation_time;
  }
}

TEST_F(WindowedFramePredictorTest, MissedLastVsync_ShouldPredictWithInterval) {
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
  EXPECT_LE(prediction.presentation_time - prediction.latch_point_time, vsync_interval);
}

TEST_F(WindowedFramePredictorTest, MissedPresentRequest_ShouldTargetNextVsync) {
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
  EXPECT_GE(prediction.latch_point_time, prediction.presentation_time - vsync_interval);
}

// The following two tests test the behavior of kHardcodedMargin. We want to be able to
// schedule close to it, but not too aggressively. If the constant changes these tests
// will likely need to change as well.
TEST_F(WindowedFramePredictorTest, AttemptsToBeLowLatency_ShouldBePossible) {
  const zx::duration update_duration = zx::msec(2);
  const zx::duration render_duration = zx::msec(5);

  // Fill the window size.
  for (int i = 0; i < 10; ++i) {
    predictor_->ReportRenderDuration(render_duration);
    predictor_->ReportUpdateDuration(update_duration);
  }

  const zx::duration vsync_interval = zx::msec(15);
  zx::time last_vsync_time = ms_to_time(15);
  zx::time requested_present = last_vsync_time + vsync_interval;
  zx::time now = requested_present - update_duration - render_duration - zx::usec(3500);
  EXPECT_GT(now, last_vsync_time);

  PredictionRequest request = {.now = now,
                               .requested_presentation_time = requested_present,
                               .last_vsync_time = last_vsync_time,
                               .vsync_interval = vsync_interval};
  auto prediction = predictor_->GetPrediction(request);

  // The prediction should be for the next vsync.
  EXPECT_LE(prediction.presentation_time.get(), last_vsync_time.get() + vsync_interval.get());
  EXPECT_GE(prediction.latch_point_time.get(), now.get());
}

TEST_F(WindowedFramePredictorTest, AttemptsToBeTooAggressive_ShouldNotBePossible) {
  const zx::duration update_duration = zx::msec(1);
  const zx::duration render_duration = zx::msec(2);

  // Fill the window size.
  for (int i = 0; i < 10; ++i) {
    predictor_->ReportRenderDuration(render_duration);
    predictor_->ReportUpdateDuration(update_duration);
  }

  const zx::duration vsync_interval = zx::msec(15);
  zx::time last_vsync_time = ms_to_time(15);
  zx::time requested_present = last_vsync_time + vsync_interval;
  zx::time now = requested_present - update_duration - render_duration - zx::usec(2000);
  EXPECT_GT(now, last_vsync_time);

  PredictionRequest request = {.now = now,
                               .requested_presentation_time = requested_present,
                               .last_vsync_time = last_vsync_time,
                               .vsync_interval = vsync_interval};
  auto prediction = predictor_->GetPrediction(request);

  // The prediction should be for the vsync after the next one (we skip one).
  EXPECT_GT(prediction.presentation_time.get(), last_vsync_time.get() + vsync_interval.get());
  EXPECT_LE(prediction.presentation_time.get(), last_vsync_time.get() + 2 * vsync_interval.get());

  // We should not have been able to schedule the frame for this vsync.
  EXPECT_LE(prediction.latch_point_time.get(), now.get() + vsync_interval.get());
}

TEST(WindowedFramePredictorMinFrameDurationTest, BasicPredictions_ShouldRespectMinFrameTime) {
  zx::duration kMinPredictedFrameDuration = zx::msec(14);
  zx::duration kInitialRenderTimePrediction = zx::msec(2);
  zx::duration kInitialUpdateTimePrediction = zx::msec(2);
  auto predictor = std::make_unique<WindowedFramePredictor>(
      kMinPredictedFrameDuration, kInitialRenderTimePrediction, kInitialUpdateTimePrediction);

  PredictionRequest request = {.now = ms_to_time(1),
                               .requested_presentation_time = ms_to_time(16),
                               .last_vsync_time = ms_to_time(0),
                               .vsync_interval = zx::msec(16)};

  auto prediction = predictor->GetPrediction(request);
  EXPECT_EQ(prediction.presentation_time - prediction.latch_point_time, kMinPredictedFrameDuration);
}

TEST(WindowedFramePredictorMinFrameDurationTest, BasicPredictions_CanPassMinFrameTime) {
  zx::duration kMinPredictedFrameDuration = zx::msec(5);
  zx::duration kInitialRenderTimePrediction = zx::msec(3);
  zx::duration kInitialUpdateTimePrediction = zx::msec(3);
  auto predictor = std::make_unique<WindowedFramePredictor>(
      kMinPredictedFrameDuration, kInitialRenderTimePrediction, kInitialUpdateTimePrediction);

  PredictionRequest request = {.now = ms_to_time(1),
                               .requested_presentation_time = ms_to_time(16),
                               .last_vsync_time = ms_to_time(0),
                               .vsync_interval = zx::msec(16)};

  auto prediction = predictor->GetPrediction(request);
  EXPECT_GT(prediction.presentation_time - prediction.latch_point_time, kMinPredictedFrameDuration);
}

TEST(WindowedFramePredictorMinFrameDurationTest,
     PredictionsAfterUpdating_ShouldRespectMinFrameTime) {
  zx::duration kMinPredictedFrameDuration = zx::msec(13);
  zx::duration kInitialRenderTimePrediction = zx::msec(2);
  zx::duration kInitialUpdateTimePrediction = zx::msec(2);
  auto predictor = std::make_unique<WindowedFramePredictor>(
      kMinPredictedFrameDuration, kInitialRenderTimePrediction, kInitialUpdateTimePrediction);

  const zx::duration update_duration = zx::msec(3);
  const zx::duration render_duration = zx::msec(3);
  const size_t kBiggerThanAllPredictionWindows = 5;
  for (size_t i = 0; i < kBiggerThanAllPredictionWindows; ++i) {
    predictor->ReportRenderDuration(render_duration);
    predictor->ReportUpdateDuration(update_duration);
  }

  PredictionRequest request = {.now = ms_to_time(1),
                               .requested_presentation_time = ms_to_time(16),
                               .last_vsync_time = ms_to_time(0),
                               .vsync_interval = zx::msec(16)};

  auto prediction = predictor->GetPrediction(request);
  EXPECT_EQ(prediction.presentation_time - prediction.latch_point_time, kMinPredictedFrameDuration);
}

// ---------------------------------------------------------------------------
// ConstantFramePredictor tests
// ---------------------------------------------------------------------------
TEST(ConstantFramePredictor, PredictionsAreConstant) {
  const zx::duration offset = zx::msec(4);
  ConstantFramePredictor predictor(offset);

  // Report durations less than the offset.
  const zx::duration update_duration = zx::msec(1);
  const zx::duration render_duration = zx::msec(2);
  EXPECT_GT(offset, update_duration + render_duration);
  for (int i = 0; i < 10; ++i) {
    predictor.ReportRenderDuration(render_duration);
    predictor.ReportUpdateDuration(update_duration);
  }

  // Prediction should always be the offset
  PredictionRequest request = {.now = ms_to_time(5),
                               .requested_presentation_time = ms_to_time(10),
                               .last_vsync_time = ms_to_time(0),
                               .vsync_interval = zx::msec(10)};
  auto prediction = predictor.GetPrediction(request);

  EXPECT_GT(prediction.presentation_time, request.now);
  EXPECT_GE(prediction.latch_point_time, request.now);
  EXPECT_EQ(prediction.latch_point_time + offset, prediction.presentation_time);
}

TEST(ConstantFramePredictor, PredictionsWithOverBudgetDurationsAreConstant) {
  const zx::duration offset = zx::msec(4);
  ConstantFramePredictor predictor(offset);
  // Report durations less than the offset.
  const zx::duration update_duration = zx::msec(5);
  const zx::duration render_duration = zx::msec(2);
  EXPECT_LT(offset, update_duration + render_duration);
  for (int i = 0; i < 10; ++i) {
    predictor.ReportRenderDuration(render_duration);
    predictor.ReportUpdateDuration(update_duration);
  }

  PredictionRequest request = {.now = ms_to_time(5),
                               .requested_presentation_time = ms_to_time(10),
                               .last_vsync_time = ms_to_time(0),
                               .vsync_interval = zx::msec(10)};
  auto prediction = predictor.GetPrediction(request);

  EXPECT_GT(prediction.presentation_time, request.now);
  EXPECT_GE(prediction.latch_point_time, request.now);
  EXPECT_EQ(prediction.latch_point_time + offset, prediction.presentation_time);
}

TEST(ConstantFramePredictor, OffsetsGreaterThanVsyncIntervalAreRespected) {
  const zx::duration offset = zx::msec(26);
  ConstantFramePredictor predictor(offset);

  // Offset does not fit within requested_presentation_time.
  PredictionRequest request = {.now = ms_to_time(17),
                               .requested_presentation_time = ms_to_time(32),
                               .last_vsync_time = ms_to_time(16),
                               .vsync_interval = zx::msec(16)};
  auto prediction = predictor.GetPrediction(request);

  EXPECT_GT(prediction.presentation_time, request.now);
  EXPECT_EQ(prediction.latch_point_time + offset, prediction.presentation_time);
  EXPECT_EQ(ms_to_time(48), prediction.presentation_time);
}

}  // namespace test
}  // namespace scheduling
