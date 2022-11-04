// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/string_printf.h>
#include <gmock/gmock.h>

#include "src/media/audio/audio_core/v1/mix_stage.h"
#include "src/media/audio/audio_core/v1/packet_queue.h"
#include "src/media/audio/audio_core/v1/testing/packet_factory.h"
#include "src/media/audio/audio_core/v1/testing/threading_model_fixture.h"
#include "src/media/audio/lib/clock/clone_mono.h"
#include "src/media/audio/lib/clock/testing/clock_test.h"

using testing::Each;
using testing::FloatEq;

namespace media::audio {
namespace {

// Used when the ReadLockContext is unused by the test.
static media::audio::ReadableStream::ReadLockContext rlctx;

enum class ClockMode { SAME, WITH_OFFSET, RATE_ADJUST };

enum class Direction { Render, Capture };

constexpr uint32_t kDefaultNumChannels = 2;
constexpr uint32_t kDefaultFrameRate = 48000;
const Format kDefaultFormat =
    Format::Create(fuchsia::media::AudioStreamType{
                       .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                       .channels = kDefaultNumChannels,
                       .frames_per_second = kDefaultFrameRate,
                   })
        .take_value();

//
// MixStageClockTest (MicroSrcTest, AdjustableClockTest)
//
// This set of tests validates how MixStage handles clock synchronization
//
// Currently, we tune PIDs by running these test cases. Most recent tuning occurred 11/01/2020.
//
// Two synchronization scenarios are validated:
//  1) Client and device clocks are non-adjustable -- apply micro-SRC (MicroSrcTest)
//  2) Client clock is adjustable -- tune this adjustable client clock (AdjustableClockTest)
//  2a) Adjustable client clock was previously adjusted, is now synching to a monotonic target --
//      tune the adjustable client clock, but with
//
// A synchronization aspect using DeviceAdjustable clocks -- device clock recovery, from driver
// position notifications -- is tested in audio_driver_clock_unittest.cc.
// Another -- fine-tuning a hardware clock to match a fixed client clock, is not yet implemented.

// With any error detection and adaptive convergence, an initial (primary) error is usually followed
// by a smaller "correction overshoot" (secondary) error of opposite magnitude.
//
// Current worst-case position error deviation, based on current PID coefficients:
//                           Major (immediate response)          Minor (overshoot)
// Worst-case error:         10-nsec-per-ppm-change              ~1 nsec-per-ppm-change
// Occurring after:          10-20 msec                          50-100 msec
//
// Thus in the absolute worst-case scenario, a rate change of 2000ppm (from -1000 adjusted, to
// +1000 adjusted) should cause worst-case desync position error of less than 20 microseconds --
// about 1 frame at 48kHz.
//
// Note: these are subject to change as we tune the PID coefficients for best performance.
//

// These multipliers (scaled by rate_adjust_ppm) determine worst-case primary/secondary error
// limits. Error is calculated by: taking the Actual long-running source position (maintained from
// the amount advanced in each Mix call) and subtracting the Expected source position (calculated by
// converting dest frame through dest and source clocks to fractional source). Thus if our Expected
// (clock-derived) source position is too high, we calculate a NEGATIVE position error.
//
// Why are these expected-error consts different signs for MicroSrc versus Adjustable/RevertToMono?
// MicroSrc mode uses the error to change the SRC rate (which is external to both clocks), whereas
// Adjustable/RevertToMono use the error to rate-adjust the source clock. MicroSrc interprets a
// positive error as "we need to consume MORE SLOWLY", whereas Adjustable/RevertToMono interpret a
// positive error as "we need to SPEED UP the source clock".
static constexpr float kMicroSrcPrimaryErrPpmMultiplier = -10.01;
static constexpr float kAdjustablePrimaryErrPpmMultiplier = 35.0;
static constexpr float kRevertToMonoPrimaryErrPpmMultiplier = 10.01;

static constexpr float kMicroSrcSecondaryErrPpmMultiplier = 0.9;
static constexpr float kAdjustableSecondaryErrPpmMultiplier = -25;
static constexpr float kRevertToMonoSecondaryErrPpmMultiplier = -0.1;

static constexpr int32_t kMicroSrcLimitMixCountOneUsecErr = 4;
static constexpr int32_t kAdjustableLimitMixCountOneUsecErr = 125;
static constexpr int32_t kRevertToMonoLimitMixCountOneUsecErr = 5;

static constexpr int32_t kMicroSrcLimitMixCountOnePercentErr = 12;
static constexpr int32_t kAdjustableLimitMixCountOnePercentErr = 175;
static constexpr int32_t kRevertToMonoLimitMixCountOnePercentErr = 5;

static constexpr int32_t kMicroSrcMixCountUntilSettled = 15;
static constexpr int32_t kAdjustableMixCountUntilSettled = 180;
static constexpr int32_t kRevertToMonoMixCountUntilSettled = 5;

// We validate Micro-SRC much faster than real-time, so we can test settling for much longer.
static constexpr int32_t kMicroSrcMixCountSettledVerificationPeriod = 1000;
static constexpr int32_t kAdjustableMixCountSettledVerificationPeriod = 20;
static constexpr int32_t kRevertToMonoMixCountSettledVerificationPeriod = 20;

// Error thresholds
static constexpr auto kMicroSrcLimitSettledErr = zx::nsec(15);
static constexpr auto kAdjustableLimitSettledErr = zx::nsec(100);
static constexpr auto kRevertToMonoLimitSettledErr = zx::nsec(10);

// When tuning a new set of PID coefficients, set this to enable additional results logging.
constexpr bool kDisplayForPidCoefficientsTuning = false;
// Verbose logging of the shape/timing of clock convergence.
constexpr bool kTraceClockSyncConvergence = false;

class MixStageClockTest : public testing::ThreadingModelFixture {
 protected:
  // We measure long-running position across mixes of 10ms (our block size).
  // TODO(fxbug.dev/56635): If our mix timeslice shortens, adjust the below and retune the PIDs.
  static constexpr zx::duration kClockSyncMixDuration = zx::msec(10);
  static constexpr uint32_t kFramesToMix =
      kDefaultFrameRate * kClockSyncMixDuration.to_msecs() / 1000;

  fbl::RefPtr<VersionedTimelineFunction> device_ref_to_frac_frames_;
  fbl::RefPtr<VersionedTimelineFunction> client_ref_to_frac_frames_;

  void VerifySync(ClockMode clock_mode, int32_t rate_adjust_ppm = 0);

  virtual void SetRateLimits(int32_t rate_adjust_ppm);
  zx::duration PrimaryErrorLimit(int32_t rate_adjust_ppm) {
    return zx::duration(rate_adjust_ppm * primary_err_ppm_multiplier_);
  }
  zx::duration SecondaryErrorLimit(int32_t rate_adjust_ppm) {
    return zx::duration(rate_adjust_ppm * secondary_err_ppm_multiplier_);
  }

  virtual void SetClocks(ClockMode clock_mode, int32_t rate_adjust_ppm) = 0;
  void ConnectStages();
  void SyncTest(int32_t rate_adjust_ppm);

  std::shared_ptr<MixStage> mix_stage_;
  std::shared_ptr<Mixer> mixer_;

  std::shared_ptr<Clock> client_clock_;
  std::shared_ptr<Clock> device_clock_;

  int32_t total_mix_count_;
  int32_t limit_mix_count_settled_;
  int32_t limit_mix_count_one_usec_err_;
  int32_t limit_mix_count_one_percent_err_;

  float primary_err_ppm_multiplier_;
  float secondary_err_ppm_multiplier_;
  zx::duration upper_limit_source_pos_err_;
  zx::duration lower_limit_source_pos_err_;

  zx::duration one_usec_err_;       // The smaller of one microsec, and our settled err value.
  zx::duration one_percent_err_;    // 1% of the maximum allowed primary error
  zx::duration limit_settled_err_;  // Largest err allowed during final kMixCountSettled mixes.

  Direction direction_;  // Does data flow client->device (Render) or device->client (Capture)
};

// MicroSrcTest uses a custom client clock, with a default non-adjustable device clock. This
// combination forces AudioCore to use "micro-SRC" to reconcile any rate differences.
class MicroSrcTest : public MixStageClockTest, public ::testing::WithParamInterface<Direction> {
  static constexpr zx::duration kClockOffset = zx::sec(42);

 protected:
  void SetUp() override {
    direction_ = GetParam();
    MixStageClockTest::SetUp();
  }

  void SetRateLimits(int32_t rate_adjust_ppm) override {
    primary_err_ppm_multiplier_ = kMicroSrcPrimaryErrPpmMultiplier;
    secondary_err_ppm_multiplier_ = kMicroSrcSecondaryErrPpmMultiplier;

    limit_mix_count_settled_ = kMicroSrcMixCountUntilSettled;
    total_mix_count_ = limit_mix_count_settled_ + kMicroSrcMixCountSettledVerificationPeriod;

    limit_mix_count_one_usec_err_ = kMicroSrcLimitMixCountOneUsecErr;
    limit_mix_count_one_percent_err_ = kMicroSrcLimitMixCountOnePercentErr;

    limit_settled_err_ = kMicroSrcLimitSettledErr;

    MixStageClockTest::SetRateLimits(rate_adjust_ppm);
  }

  // Establish reference clocks and ref-clock-to-frac-frame transforms for both client and device,
  // depending on which synchronization mode is being tested.
  void SetClocks(ClockMode clock_mode, int32_t rate_adjust_ppm) override {
    device_ref_to_frac_frames_ = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
        0, context().clock_factory()->mono_time().get(),
        Fixed(kDefaultFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs()));

    device_clock_ = context().clock_factory()->CreateDeviceFixed(clock::CloneOfMonotonic(),
                                                                 Clock::kMonotonicDomain);
    {
      SCOPED_TRACE("device clock must advance");
      clock::testing::VerifyAdvances(*device_clock_, context().clock_factory()->synthetic());
    }

    zx::time source_start = context().clock_factory()->mono_time();
    if (clock_mode == ClockMode::WITH_OFFSET) {
      source_start += kClockOffset;
    }

    client_ref_to_frac_frames_ = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
        0, source_start.get(), Fixed(kDefaultFormat.frames_per_second()).raw_value(),
        zx::sec(1).to_nsecs()));

    client_clock_ = context().clock_factory()->CreateClientFixed(
        source_start, clock_mode == ClockMode::RATE_ADJUST ? rate_adjust_ppm : 0);
    {
      SCOPED_TRACE("client clock must advance");
      clock::testing::VerifyAdvances(*client_clock_, context().clock_factory()->synthetic());
    }
  }
};

// AdjustableClockTest uses the AudioCore flexible client clock along with a non-adjustable device
// clock. AudioCore will adjust the flexible clock to reconcile any rate differences.
class AdjustableClockTest : public MixStageClockTest,
                            public ::testing::WithParamInterface<Direction> {
  static constexpr uint32_t kNonMonotonicDomain = 42;
  static constexpr zx::duration kClockOffset = zx::sec(68);

 protected:
  void SetUp() override {
    direction_ = GetParam();
    MixStageClockTest::SetUp();
  }

  void SetRateLimits(int32_t rate_adjust_ppm) override {
    primary_err_ppm_multiplier_ = kAdjustablePrimaryErrPpmMultiplier;
    secondary_err_ppm_multiplier_ = kAdjustableSecondaryErrPpmMultiplier;

    limit_mix_count_settled_ = kAdjustableMixCountUntilSettled;
    total_mix_count_ = limit_mix_count_settled_ + kAdjustableMixCountSettledVerificationPeriod;

    limit_mix_count_one_usec_err_ = kAdjustableLimitMixCountOneUsecErr;
    limit_mix_count_one_percent_err_ = kAdjustableLimitMixCountOnePercentErr;

    limit_settled_err_ = kAdjustableLimitSettledErr;

    MixStageClockTest::SetRateLimits(rate_adjust_ppm);
  }

  // Establish reference clocks and ref-clock-to-frac-frame transforms for both client and device,
  // depending on which synchronization mode is being tested.
  void SetClocks(ClockMode clock_mode, int32_t rate_adjust_ppm) override {
    client_ref_to_frac_frames_ = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
        0, context().clock_factory()->mono_time().get(),
        Fixed(kDefaultFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs()));

    client_clock_ =
        context().clock_factory()->CreateClientAdjustable(clock::AdjustableCloneOfMonotonic());
    {
      SCOPED_TRACE("client clock must advance");
      clock::testing::VerifyAdvances(*client_clock_, context().clock_factory()->synthetic());
    }

    auto device_start = context().clock_factory()->mono_time();
    if (clock_mode == ClockMode::WITH_OFFSET) {
      device_start += kClockOffset;
    }

    device_ref_to_frac_frames_ = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
        0, device_start.get(), Fixed(kDefaultFormat.frames_per_second()).raw_value(),
        zx::sec(1).to_nsecs()));

    device_clock_ = context().clock_factory()->CreateDeviceFixed(
        device_start, clock_mode == ClockMode::RATE_ADJUST ? rate_adjust_ppm : 0,
        kNonMonotonicDomain);
    {
      SCOPED_TRACE("device clock must advance");
      clock::testing::VerifyAdvances(*device_clock_, context().clock_factory()->synthetic());
    }
  }
};

// RevertToMonoTest uses a AudioCore flexible clock that has been tuned away from 0 ppm, with a
// monotonic device clock. AudioCore adjusts the flex clock linearly, to reconcile rate/position
// differences with the monotonic clock as rapidly as possible.
class RevertToMonoTest : public MixStageClockTest, public ::testing::WithParamInterface<Direction> {
  static constexpr zx::duration kClockOffset = zx::sec(243);

 protected:
  void SetUp() override {
    direction_ = GetParam();
    MixStageClockTest::SetUp();
  }

  void SetRateLimits(int32_t rate_adjust_ppm) override {
    primary_err_ppm_multiplier_ = kRevertToMonoPrimaryErrPpmMultiplier;
    secondary_err_ppm_multiplier_ = kRevertToMonoSecondaryErrPpmMultiplier;

    limit_mix_count_settled_ = kRevertToMonoMixCountUntilSettled;
    total_mix_count_ = limit_mix_count_settled_ + kRevertToMonoMixCountSettledVerificationPeriod;

    limit_mix_count_one_usec_err_ = kRevertToMonoLimitMixCountOneUsecErr;
    limit_mix_count_one_percent_err_ = kRevertToMonoLimitMixCountOnePercentErr;

    limit_settled_err_ = kRevertToMonoLimitSettledErr;

    MixStageClockTest::SetRateLimits(rate_adjust_ppm);
  }

  // To test RevertSourceToMonotonic/RevertDestToMonotonic clock sync modes, we use an adjustable
  // client clock, with a device clock in the monotonic domain. To test the clock when it must
  // adjust UPWARD by rate_adjust_ppm, we initially set it TOO LOW (note -rate_adjust_ppm below).
  void SetClocks(ClockMode clock_mode, int32_t rate_adjust_ppm) override {
    client_ref_to_frac_frames_ = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
        0, context().clock_factory()->mono_time().get(),
        Fixed(kDefaultFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs()));

    zx::clock::update_args args;
    args.reset().set_rate_adjust(-rate_adjust_ppm);
    zx::clock adjusted_clock = clock::AdjustableCloneOfMonotonic();
    EXPECT_EQ(adjusted_clock.update(args), ZX_OK);

    client_clock_ = context().clock_factory()->CreateClientAdjustable(std::move(adjusted_clock));
    {
      SCOPED_TRACE("client clock must advance");
      clock::testing::VerifyAdvances(*client_clock_, context().clock_factory()->synthetic());
    }

    auto device_start = context().clock_factory()->mono_time();
    if (clock_mode == ClockMode::WITH_OFFSET) {
      device_start += kClockOffset;
    }

    device_ref_to_frac_frames_ = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
        0, device_start.get(), Fixed(kDefaultFormat.frames_per_second()).raw_value(),
        zx::sec(1).to_nsecs()));

    device_clock_ =
        context().clock_factory()->CreateDeviceFixed(device_start, 0, Clock::kMonotonicDomain);
    {
      SCOPED_TRACE("device clock must advance");
      clock::testing::VerifyAdvances(*device_clock_, context().clock_factory()->synthetic());
    }
  }
};

// Set the limits for worst-case source position error during this mix interval
//
void MixStageClockTest::SetRateLimits(int32_t rate_adjust_ppm) {
  if (direction_ == Direction::Capture) {
    primary_err_ppm_multiplier_ = -primary_err_ppm_multiplier_;
    secondary_err_ppm_multiplier_ = -secondary_err_ppm_multiplier_;
  }

  // Set the limits for worst-case source position error during this mix interval
  // If clock runs fast, our initial error is negative (position too low), followed by smaller
  // positive error (position too high). These are reversed if clock runs slow.
  auto primary_err_limit = PrimaryErrorLimit(rate_adjust_ppm);
  auto secondary_err_limit = SecondaryErrorLimit(rate_adjust_ppm);

  // Max positive and negative error values are determined by the magnitude of rate adjustment. At
  // very small rate_adjust_ppm, these values can be overshadowed by any steady-state "ripple" we
  // might have, so include that "ripple" value in our max/min and 1% errors.
  auto min_max = std::minmax(primary_err_limit, secondary_err_limit);
  lower_limit_source_pos_err_ = min_max.first - limit_settled_err_;
  upper_limit_source_pos_err_ = min_max.second + limit_settled_err_;

  one_usec_err_ = std::max(limit_settled_err_, zx::usec(1));
  auto primary_err_one_percent = zx::duration(std::abs(primary_err_limit.get()) / 100);
  one_percent_err_ = std::max(limit_settled_err_, primary_err_one_percent);

  limit_mix_count_one_usec_err_ = std::min(limit_mix_count_one_usec_err_, limit_mix_count_settled_);
  limit_mix_count_one_percent_err_ =
      std::min(limit_mix_count_one_percent_err_, limit_mix_count_settled_);
}

void MixStageClockTest::ConnectStages() {
  std::shared_ptr<PacketQueue> packet_queue;

  if (direction_ == Direction::Render) {
    // Create a PacketQueue with the client timeline and clock, as the source.
    // Pass the device timeline and clock to a mix stage, as the destination.
    packet_queue = std::make_shared<PacketQueue>(kDefaultFormat, client_ref_to_frac_frames_,
                                                 std::move(client_clock_));
    mix_stage_ = std::make_shared<MixStage>(kDefaultFormat, kFramesToMix,
                                            device_ref_to_frac_frames_, device_clock_);
  } else {
    // Create a PacketQueue with the device timeline and clock, as the source.
    // Pass the client timeline and clock to a mix stage, as the destination.
    packet_queue = std::make_shared<PacketQueue>(kDefaultFormat, device_ref_to_frac_frames_,
                                                 std::move(device_clock_));
    mix_stage_ = std::make_shared<MixStage>(kDefaultFormat, kFramesToMix,
                                            client_ref_to_frac_frames_, client_clock_);
  }

  // Connect packet queue to mix stage.
  mixer_ = mix_stage_->AddInput(packet_queue);
}

// Set up the various prerequisites of a clock synchronization test, then execute the test.
void MixStageClockTest::VerifySync(ClockMode clock_mode, int32_t rate_adjust_ppm) {
  SetRateLimits(rate_adjust_ppm);

  SetClocks(clock_mode, rate_adjust_ppm);

  ConnectStages();

  SyncTest(rate_adjust_ppm);
}

// Test accuracy of long-running position maintained by MixStage across ReadLock calls. No audio
// is streamed: source position is determined by clocks and change in dest position.
//
// Rate adjustment is resolved by a feedback control, so run the mix for a significant interval,
// measuring worst-case source position error. We separately note worst-case source position error
// during the final mixes, to assess the "settled" state. The overall worst-case error observed
// should be proportional to the magnitude of rate change, whereas once we settle to steady state
// our position desync error should have a ripple of much less than 1 usec.
//
void MixStageClockTest::SyncTest(int32_t rate_adjust_ppm) {
  auto& state = mixer_->state();

  zx::duration max_err{0}, max_settled_err{0};
  zx::duration min_err{0}, min_settled_err{0};
  int32_t mix_count_of_max_err = -1, mix_count_of_min_err = -1;
  int32_t actual_mix_count_one_percent_err = -1, actual_mix_count_one_usec_err = -1;
  int32_t actual_mix_count_settled = -1;

  for (auto mix_count = 0; mix_count < total_mix_count_; ++mix_count) {
    // Advance time by kClockSyncMixDuration after the first mix.
    if (mix_count) {
      context().clock_factory()->AdvanceMonoTimeBy(kClockSyncMixDuration);
    }

    mix_stage_->ReadLock(rlctx, Fixed(kFramesToMix * mix_count), kFramesToMix);
    ASSERT_EQ(state.next_dest_frame(), kFramesToMix * (mix_count + 1));

    // Track the worst-case position errors (overall min/max, 1%, 1us, final-settled).
    if (state.source_pos_error() > max_err) {
      max_err = state.source_pos_error();
      mix_count_of_max_err = mix_count;
    }
    if (state.source_pos_error() < min_err) {
      min_err = state.source_pos_error();
      mix_count_of_min_err = mix_count;
    }
    auto abs_source_pos_error = zx::duration(std::abs(state.source_pos_error().get()));
    if (abs_source_pos_error > one_percent_err_) {
      actual_mix_count_one_percent_err = mix_count;
    }
    if (abs_source_pos_error > one_usec_err_) {
      actual_mix_count_one_usec_err = mix_count;
    }
    if (abs_source_pos_error > limit_settled_err_) {
      actual_mix_count_settled = mix_count;
    }

    if (mix_count >= limit_mix_count_settled_) {
      max_settled_err = std::max(state.source_pos_error(), max_settled_err);
      min_settled_err = std::min(state.source_pos_error(), min_settled_err);
    }

    if constexpr (kTraceClockSyncConvergence) {
      FX_LOGS(INFO) << "Testing " << rate_adjust_ppm << " PPM: [" << std::right << std::setw(3)
                    << mix_count << "], error " << std::setw(5) << state.source_pos_error().get();
    }
  }

  EXPECT_LE(max_err.get(), upper_limit_source_pos_err_.get())
      << "rate ppm " << rate_adjust_ppm << " at mix_count[" << mix_count_of_max_err << "] "
      << mix_count_of_max_err * kClockSyncMixDuration.to_msecs() << "ms";
  EXPECT_GE(min_err.get(), lower_limit_source_pos_err_.get())
      << "rate ppm " << rate_adjust_ppm << " at mix_count[" << mix_count_of_min_err << "] "
      << mix_count_of_min_err * kClockSyncMixDuration.to_msecs() << "ms";

  if (rate_adjust_ppm != 0) {
    EXPECT_LE(actual_mix_count_one_usec_err, limit_mix_count_one_usec_err_)
        << "rate ppm " << rate_adjust_ppm << " took too long to settle to "
        << limit_settled_err_.get() << " nanosec (1 microsecond)";

    EXPECT_LE(actual_mix_count_one_percent_err, limit_mix_count_one_percent_err_)
        << "rate ppm " << rate_adjust_ppm
        << " took too long to settle to 1% of initial worst-case desync " << one_percent_err_.get()
        << " nanosec: actual [" << actual_mix_count_one_percent_err << "] mixes, limit ["
        << limit_mix_count_one_percent_err_ << "] mixes";
  }

  EXPECT_LE(max_settled_err.get(), limit_settled_err_.get()) << "rate ppm " << rate_adjust_ppm;
  EXPECT_GE(min_settled_err.get(), -(limit_settled_err_.get())) << "rate ppm " << rate_adjust_ppm;

  if constexpr (kDisplayForPidCoefficientsTuning) {
    if (rate_adjust_ppm != 0) {
      FX_LOGS(INFO)
          << "****************************************************************************";
      if (zx::duration(std::abs(lower_limit_source_pos_err_.get())) > upper_limit_source_pos_err_) {
        FX_LOGS(INFO)
            << fbl::StringPrintf(
                   "Rate %5d: Primary [%2d] %5ld (%5ld limit); Secondary [%2d] %5ld (%5ld limit)",
                   rate_adjust_ppm, mix_count_of_min_err, min_err.get(),
                   lower_limit_source_pos_err_.get(), mix_count_of_max_err, max_err.get(),
                   upper_limit_source_pos_err_.get())
                   .c_str();
      } else {
        FX_LOGS(INFO)
            << fbl::StringPrintf(
                   "Rate %5d: Primary [%2d] %5ld (%5ld limit); Secondary [%2d] %5ld (%5ld limit)",
                   rate_adjust_ppm, mix_count_of_max_err, max_err.get(),
                   upper_limit_source_pos_err_.get(), mix_count_of_min_err, min_err.get(),
                   lower_limit_source_pos_err_.get())
                   .c_str();
      }

      FX_LOGS(INFO) << (actual_mix_count_one_usec_err <= limit_mix_count_one_usec_err_
                            ? "Converged by  ["
                            : "NOT converged [")
                    << std::setw(2) << actual_mix_count_one_usec_err + 1 << "] (" << std::setw(2)
                    << limit_mix_count_one_usec_err_ << " limit) to 1us  (" << std::setw(3)
                    << one_usec_err_.get() << ")";
      FX_LOGS(INFO) << (actual_mix_count_one_percent_err <= limit_mix_count_one_percent_err_
                            ? "Converged by  ["
                            : "NOT converged [")
                    << std::setw(2) << actual_mix_count_one_percent_err + 1 << "] (" << std::setw(2)
                    << limit_mix_count_one_percent_err_ << " limit) to 1%   (" << std::setw(3)
                    << one_percent_err_.get() << ")";
      FX_LOGS(INFO) << "Final-settled [" << std::setw(2) << actual_mix_count_settled << "] ("
                    << std::setw(2) << limit_mix_count_settled_ << " limit) to "
                    << max_settled_err.get() << "/" << std::setw(2) << min_settled_err.get() << " ("
                    << limit_settled_err_.get() << " limit)";
    }
  }
}

// MicroSrc sync mode does not rate-adjust a zx::clock, whereas AdjustSource|DestClock and
// RevertSource|DestToMonotonic modes do. Zircon clocks cannot adjust beyond [-1000, +1000] PPM,
// hindering our ability to chase device clocks running close to that limit. This is why
// MicroSrcTest tests "Up1000" and "Down1000", while AdjustableClockTest and RevertToMonoTest use
// a reasonable validation outer limit of 750 PPM.

// Test cases that validate the MixStage+Clock "micro-SRC" synchronization path.
TEST_P(MicroSrcTest, Basic) { VerifySync(ClockMode::SAME); }
TEST_P(MicroSrcTest, Offset) { VerifySync(ClockMode::WITH_OFFSET); }

TEST_P(MicroSrcTest, AdjustUp1) { VerifySync(ClockMode::RATE_ADJUST, 1); }
TEST_P(MicroSrcTest, AdjustDown1) { VerifySync(ClockMode::RATE_ADJUST, -1); }

TEST_P(MicroSrcTest, AdjustUp2) { VerifySync(ClockMode::RATE_ADJUST, 2); }
TEST_P(MicroSrcTest, AdjustDown2) { VerifySync(ClockMode::RATE_ADJUST, -2); }

TEST_P(MicroSrcTest, AdjustUp3) { VerifySync(ClockMode::RATE_ADJUST, 3); }
TEST_P(MicroSrcTest, AdjustDown3) { VerifySync(ClockMode::RATE_ADJUST, -3); }

TEST_P(MicroSrcTest, AdjustUp10) { VerifySync(ClockMode::RATE_ADJUST, 10); }
TEST_P(MicroSrcTest, AdjustDown10) { VerifySync(ClockMode::RATE_ADJUST, -10); }

TEST_P(MicroSrcTest, AdjustUp30) { VerifySync(ClockMode::RATE_ADJUST, 30); }
TEST_P(MicroSrcTest, AdjustDown30) { VerifySync(ClockMode::RATE_ADJUST, -30); }

TEST_P(MicroSrcTest, AdjustUp100) { VerifySync(ClockMode::RATE_ADJUST, 100); }
TEST_P(MicroSrcTest, AdjustDown100) { VerifySync(ClockMode::RATE_ADJUST, -100); }

TEST_P(MicroSrcTest, AdjustUp300) { VerifySync(ClockMode::RATE_ADJUST, 300); }
TEST_P(MicroSrcTest, AdjustDown300) { VerifySync(ClockMode::RATE_ADJUST, -300); }

TEST_P(MicroSrcTest, AdjustUp1000) { VerifySync(ClockMode::RATE_ADJUST, 1000); }
TEST_P(MicroSrcTest, AdjustDown1000) { VerifySync(ClockMode::RATE_ADJUST, -1000); }

// Test cases that validate the MixStage+Clock "flexible clock" synchronization path.
TEST_P(AdjustableClockTest, Basic) { VerifySync(ClockMode::SAME); }
TEST_P(AdjustableClockTest, Offset) { VerifySync(ClockMode::WITH_OFFSET); }

TEST_P(AdjustableClockTest, AdjustUp1) { VerifySync(ClockMode::RATE_ADJUST, 1); }
TEST_P(AdjustableClockTest, AdjustDown1) { VerifySync(ClockMode::RATE_ADJUST, -1); }

TEST_P(AdjustableClockTest, AdjustUp2) { VerifySync(ClockMode::RATE_ADJUST, 2); }
TEST_P(AdjustableClockTest, AdjustDown2) { VerifySync(ClockMode::RATE_ADJUST, -2); }

TEST_P(AdjustableClockTest, AdjustUp3) { VerifySync(ClockMode::RATE_ADJUST, 3); }
TEST_P(AdjustableClockTest, AdjustDown3) { VerifySync(ClockMode::RATE_ADJUST, -3); }

TEST_P(AdjustableClockTest, AdjustUp10) { VerifySync(ClockMode::RATE_ADJUST, 10); }
TEST_P(AdjustableClockTest, AdjustDown10) { VerifySync(ClockMode::RATE_ADJUST, -10); }

TEST_P(AdjustableClockTest, AdjustUp30) { VerifySync(ClockMode::RATE_ADJUST, 30); }
TEST_P(AdjustableClockTest, AdjustDown30) { VerifySync(ClockMode::RATE_ADJUST, -30); }

TEST_P(AdjustableClockTest, AdjustUp100) { VerifySync(ClockMode::RATE_ADJUST, 100); }
TEST_P(AdjustableClockTest, AdjustDown100) { VerifySync(ClockMode::RATE_ADJUST, -100); }

TEST_P(AdjustableClockTest, AdjustUp300) { VerifySync(ClockMode::RATE_ADJUST, 300); }
TEST_P(AdjustableClockTest, AdjustDown300) { VerifySync(ClockMode::RATE_ADJUST, -300); }

TEST_P(AdjustableClockTest, AdjustUp750) { VerifySync(ClockMode::RATE_ADJUST, 750); }
TEST_P(AdjustableClockTest, AdjustDown750) { VerifySync(ClockMode::RATE_ADJUST, -750); }

// Test cases to validate the MixStage+Clock "flex clock reverts to monotonic target" path.
TEST_P(RevertToMonoTest, Basic) { VerifySync(ClockMode::SAME); }
TEST_P(RevertToMonoTest, Offset) { VerifySync(ClockMode::WITH_OFFSET); }

TEST_P(RevertToMonoTest, AdjustUp1) { VerifySync(ClockMode::RATE_ADJUST, 1); }
TEST_P(RevertToMonoTest, AdjustDown1) { VerifySync(ClockMode::RATE_ADJUST, -1); }

TEST_P(RevertToMonoTest, AdjustUp2) { VerifySync(ClockMode::RATE_ADJUST, 2); }
TEST_P(RevertToMonoTest, AdjustDown2) { VerifySync(ClockMode::RATE_ADJUST, -2); }

TEST_P(RevertToMonoTest, AdjustUp3) { VerifySync(ClockMode::RATE_ADJUST, 3); }
TEST_P(RevertToMonoTest, AdjustDown3) { VerifySync(ClockMode::RATE_ADJUST, -3); }

TEST_P(RevertToMonoTest, AdjustUp10) { VerifySync(ClockMode::RATE_ADJUST, 10); }
TEST_P(RevertToMonoTest, AdjustDown10) { VerifySync(ClockMode::RATE_ADJUST, -10); }

TEST_P(RevertToMonoTest, AdjustUp30) { VerifySync(ClockMode::RATE_ADJUST, 30); }
TEST_P(RevertToMonoTest, AdjustDown30) { VerifySync(ClockMode::RATE_ADJUST, -30); }

TEST_P(RevertToMonoTest, AdjustUp100) { VerifySync(ClockMode::RATE_ADJUST, 100); }
TEST_P(RevertToMonoTest, AdjustDown100) { VerifySync(ClockMode::RATE_ADJUST, -100); }

TEST_P(RevertToMonoTest, AdjustUp300) { VerifySync(ClockMode::RATE_ADJUST, 300); }
TEST_P(RevertToMonoTest, AdjustDown300) { VerifySync(ClockMode::RATE_ADJUST, -300); }

TEST_P(RevertToMonoTest, AdjustUp750) { VerifySync(ClockMode::RATE_ADJUST, 750); }
TEST_P(RevertToMonoTest, AdjustDown750) { VerifySync(ClockMode::RATE_ADJUST, -750); }

// Test subclasses are parameterized to run in render and capture directions.
// Thus every clock type is tested as a source and as a destination.
template <typename TestClass>
std::string PrintDirectionParam(
    const ::testing::TestParamInfo<typename TestClass::ParamType>& state) {
  return (state.param == Direction::Render ? "Render" : "Capture");
}

#define INSTANTIATE_SYNC_TEST_SUITE(_test_class_name)                                \
  INSTANTIATE_TEST_SUITE_P(ClockSync, _test_class_name,                              \
                           ::testing::Values(Direction::Render, Direction::Capture), \
                           PrintDirectionParam<_test_class_name>)

INSTANTIATE_SYNC_TEST_SUITE(MicroSrcTest);
INSTANTIATE_SYNC_TEST_SUITE(AdjustableClockTest);
INSTANTIATE_SYNC_TEST_SUITE(RevertToMonoTest);

}  // namespace

}  // namespace media::audio
