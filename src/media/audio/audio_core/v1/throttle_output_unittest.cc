// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/throttle_output.h"

#include "src/media/audio/audio_core/v1/audio_device_manager.h"
#include "src/media/audio/audio_core/v1/testing/threading_model_fixture.h"

namespace media::audio {
namespace {

class TestThrottleOutput : public ThrottleOutput {
 public:
  TestThrottleOutput(const DeviceConfig& config, ThreadingModel* threading_model,
                     DeviceRegistry* registry, LinkMatrix* link_matrix,
                     std::shared_ptr<AudioCoreClockFactory> clock_factory)
      : ThrottleOutput(config, threading_model, registry, link_matrix, clock_factory) {}

  using ThrottleOutput::driver_ref_time_to_frac_presentation_frame;
  using ThrottleOutput::last_sched_time_mono;
  using ThrottleOutput::StartMixJob;
};

class ThrottleOutputTest : public testing::ThreadingModelFixture {
 protected:
  void SetUp() override {
    throttle_output_ = std::make_shared<TestThrottleOutput>(
        context().process_config().device_config(), &threading_model(), &context().device_manager(),
        &context().link_matrix(), context().clock_factory());
  }

  std::shared_ptr<TestThrottleOutput> throttle_output_;
};

TEST_F(ThrottleOutputTest, NextTrimTime) {
  // After a mix job in the past, the next Trim will be TRIM_PERIOD beyond the most recent one.
  auto last_trim_mono_time = throttle_output_->last_sched_time_mono();
  auto past_ref_time = throttle_output_->reference_clock()->ReferenceTimeFromMonotonicTime(
      last_trim_mono_time - zx::min(1));

  throttle_output_->StartMixJob(past_ref_time);
  auto next_trim_mono_time = throttle_output_->last_sched_time_mono();
  EXPECT_EQ(next_trim_mono_time, last_trim_mono_time + TRIM_PERIOD);

  // If we start a mix job in the future, our next Trim time will be TRIM_PERIOD beyond that.
  auto future_ref_time = throttle_output_->reference_clock()->now() + zx::min(5);
  auto future_mono_time =
      throttle_output_->reference_clock()->MonotonicTimeFromReferenceTime(future_ref_time);

  throttle_output_->StartMixJob(future_ref_time);
  next_trim_mono_time = throttle_output_->last_sched_time_mono();
  EXPECT_EQ(next_trim_mono_time, future_mono_time + TRIM_PERIOD);
}

TEST_F(ThrottleOutputTest, ThrottleHasGoodClock) {
  const auto want_frames_per_ns = throttle_output_->output_pipeline()->format().frames_per_ns();
  const auto got_frac_frames_per_ns =
      throttle_output_->driver_ref_time_to_frac_presentation_frame().rate();

  const auto want_frames_per_sec = want_frames_per_ns.Scale(zx::sec(1).get());
  const auto got_frames_per_ns =
      Fixed::FromRaw(got_frac_frames_per_ns.Scale(zx::sec(1).get())).Floor();

  EXPECT_EQ(want_frames_per_sec, got_frames_per_ns);
}

}  // namespace
}  // namespace media::audio
