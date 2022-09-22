// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/time.h>

#include <memory>
#include <vector>

#include <fbl/string_printf.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fidl/fuchsia.mediastreams/cpp/wire_types.h"
#include "src/media/audio/lib/clock/clock_synchronizer.h"
#include "src/media/audio/lib/clock/clone_mono.h"
#include "src/media/audio/lib/clock/synthetic_clock_realm.h"
#include "src/media/audio/lib/clock/testing/clock_test.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/mix/mixer_source.h"
#include "src/media/audio/services/mixer/mix/ptr_decls.h"
#include "src/media/audio/services/mixer/mix/simple_packet_queue_producer_stage.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"

// The set of tests below validates how `MixerSource` handles clock synchronization.
//
// Currently, we tune PIDs by running these test cases. Most recent tuning occurred 11/01/2020.
//
// Three synchronization scenarios are validated:
//  1) Client and device clocks are non-adjustable, which should apply micro-SRC (`MicroSrcTest`).
//  2) Client clock is adjustable, which should be tuned (`AdjustableClockTest`)
//  3) Client clock was previously adjusted, and is now synching to a monotonic clock, which should
//     be tuned back (`RevertToMonoTest`).
//
// With any error detection and adaptive convergence, an initial (primary) error is usually followed
// by a smaller "correction overshoot" (secondary) error of opposite magnitude.
//
// Current worst-case position error deviation, based on current PID coefficients:
//
// ```
//                           Major (immediate response)          Minor (overshoot)
// Worst-case error:         10-nsec-per-ppm-change              ~1 nsec-per-ppm-change
// Occurring after:          10-20 msec                          50-100 msec
// ```
//
// Thus in the absolute worst-case scenario, a rate change of 2000ppm (from -1000 adjusted, to
// +1000 adjusted) should cause worst-case desync position error of less than 20 microseconds, which
// is about 1 frame at 48kHz.
//
// Note: these are subject to change as we tune the PID coefficients for best performance.

namespace media_audio {
namespace {

using ::fuchsia_mediastreams::wire::AudioSampleFormat;
namespace clock = ::media::audio::clock;

enum class ClockMode { kSame, kWithOffset, kRateAdjust };
enum class Direction { kRender, kCapture };

constexpr uint32_t kDefaultNumChannels = 2;
constexpr uint32_t kDefaultFrameRate = 48000;
const auto kDefaultFormat =
    Format::CreateOrDie({AudioSampleFormat::kFloat, kDefaultNumChannels, kDefaultFrameRate});

// These multipliers (scaled by `rate_adjust_ppm`) determine worst-case primary/secondary error
// limits. Error is calculated by; taking the actual long-running source position (maintained from
// the amount advanced in each `Mix` call) and subtracting the expected source position (calculated
// by converting destination frame through destination and source clocks to fractional source). Thus
// if our expected (clock-derived) source position is too high, we calculate a negative position
// error.
//
// # Why are these expected-error constants have different signs for `MicroSrcTest` as opposed to
//   `AdjustableClockTest` and `RevertToMonoTest`?
//
// `WithMicroSrc` mode uses the position error to change the SRC rate (which is external to both
// clocks), whereas `WithAdjustment` mode uses the error to rate-adjust the source clock. Micro-SRC
// interprets a positive error as "we need to consume more slowly", whereas adjustment interprets a
// positive error as "we need to speed up the source clock".
constexpr float kMicroSrcPrimaryErrPpmMultiplier = -10.01f;
constexpr float kAdjustablePrimaryErrPpmMultiplier = 35.0f;
constexpr float kRevertToMonoPrimaryErrPpmMultiplier = 10.01f;

constexpr float kMicroSrcSecondaryErrPpmMultiplier = 0.9f;
constexpr float kAdjustableSecondaryErrPpmMultiplier = -25.0f;
constexpr float kRevertToMonoSecondaryErrPpmMultiplier = -0.1f;

constexpr int32_t kMicroSrcLimitMixCountOneUsecErr = 4;
constexpr int32_t kAdjustableLimitMixCountOneUsecErr = 125;
constexpr int32_t kRevertToMonoLimitMixCountOneUsecErr = 5;

constexpr int32_t kMicroSrcLimitMixCountOnePercentErr = 12;
constexpr int32_t kAdjustableLimitMixCountOnePercentErr = 175;
constexpr int32_t kRevertToMonoLimitMixCountOnePercentErr = 5;

constexpr int32_t kMicroSrcMixCountUntilSettled = 15;
constexpr int32_t kAdjustableMixCountUntilSettled = 180;
constexpr int32_t kRevertToMonoMixCountUntilSettled = 5;

// We validate micro-SRC much faster than real-time, so we can test settling for much longer.
constexpr int32_t kMicroSrcMixCountSettledVerificationPeriod = 1000;
constexpr int32_t kAdjustableMixCountSettledVerificationPeriod = 20;
constexpr int32_t kRevertToMonoMixCountSettledVerificationPeriod = 20;

// Error thresholds.
constexpr auto kMicroSrcLimitSettledErr = zx::nsec(15);
constexpr auto kAdjustableLimitSettledErr = zx::nsec(100);
constexpr auto kRevertToMonoLimitSettledErr = zx::nsec(10);

// When tuning a new set of PID coefficients, set this to enable additional results logging.
constexpr bool kDisplayForPidCoefficientsTuning = false;
// Verbose logging of the shape/timing of clock convergence.
constexpr bool kTraceClockSyncConvergence = false;

// We measure long-running position across mixes of 10ms (our block size).
constexpr zx::duration kClockSyncMixDuration = zx::msec(10);
constexpr uint32_t kFramesToMix = kDefaultFrameRate * kClockSyncMixDuration.to_msecs() / 1000;

class MixerSourceClockTest : public testing::Test {
 protected:
  void RunSyncTest(ClockMode clock_mode, int32_t rate_adjust_ppm = 0) {
    SetRateLimits(rate_adjust_ppm);
    SetClocks(clock_mode, rate_adjust_ppm);

    auto source_clock = direction_ == Direction::kRender ? client_clock_ : device_clock_;
    auto dest_clock = direction_ == Direction::kRender ? device_clock_ : client_clock_;

    auto clock_sync = ClockSynchronizer::SelectModeAndCreate(source_clock, dest_clock);
    auto sampler = Sampler::Create(kDefaultFormat, kDefaultFormat,
                                   (clock_sync->mode() == ClockSynchronizer::Mode::WithMicroSRC)
                                       ? Sampler::Type::kSincSampler
                                       : Sampler::Type::kDefault);
    state_ = &sampler->state();
    auto packet_queue =
        std::make_shared<SimplePacketQueueProducerStage>(SimplePacketQueueProducerStage::Args{
            .name = "packet_queue",
            .format = kDefaultFormat,
            .reference_clock = UnreadableClock(source_clock),
        });
    packet_queue->UpdatePresentationTimeToFracFrame(
        direction_ == Direction::kRender ? client_ref_to_frac_frames_ : device_ref_to_frac_frames_);

    mixer_source_ = std::make_shared<MixerSource>(
        std::move(packet_queue),
        PipelineStage::AddSourceOptions{.clock_sync = std::move(clock_sync),
                                        .sampler = std::move(sampler)},
        std::unordered_set<GainControlId>{}, kFramesToMix);

    TestSync(rate_adjust_ppm);
  }

  virtual void SetClocks(ClockMode clock_mode, int32_t rate_adjust_ppm) = 0;

  virtual void SetRateLimits(int32_t rate_adjust_ppm) {
    if (direction_ == Direction::kCapture) {
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

    limit_mix_count_one_usec_err_ =
        std::min(limit_mix_count_one_usec_err_, limit_mix_count_settled_);
    limit_mix_count_one_percent_err_ =
        std::min(limit_mix_count_one_percent_err_, limit_mix_count_settled_);
  }

  // Tests accuracy of long-running position maintained by `MixerSource` across `Mix` calls. No
  // audio is streamed: source position is determined by clocks and change in destination position.
  //
  // Rate adjustment is resolved by a feedback control, so run the mix for a significant interval,
  // measuring worst-case source position error. We separately note worst-case source position error
  // during the final mixes, to assess the "settled" state_-> The overall worst-case error observed
  // should be proportional to the magnitude of rate change, whereas once we settle to steady state
  // our position desync error should have a ripple of much less than 1 usec.
  void TestSync(int32_t rate_adjust_ppm) {
    zx::duration max_err{0}, max_settled_err{0};
    zx::duration min_err{0}, min_settled_err{0};
    int32_t mix_count_of_max_err = -1, mix_count_of_min_err = -1;
    int32_t actual_mix_count_one_percent_err = -1, actual_mix_count_one_usec_err = -1;
    int32_t actual_mix_count_settled = -1;

    std::vector<float> dest_samples(kFramesToMix * kDefaultNumChannels, 0.0f);
    for (auto mix_count = 0; mix_count < total_mix_count_; ++mix_count) {
      // Advance time by kClockSyncMixDuration after the first mix.
      if (mix_count) {
        clock_realm_->AdvanceBy(kClockSyncMixDuration);
      }

      mixer_source_->Mix(DefaultCtx(),
                         direction_ == Direction::kRender ? device_ref_to_frac_frames_
                                                          : client_ref_to_frac_frames_,
                         Fixed(kFramesToMix * mix_count), kFramesToMix, dest_samples.data(), false);
      ASSERT_EQ(state_->next_dest_frame(), kFramesToMix * (mix_count + 1));

      // Track the worst-case position errors (overall min/max, 1%, 1us, final-settled).
      if (state_->source_pos_error() > max_err) {
        max_err = state_->source_pos_error();
        mix_count_of_max_err = mix_count;
      }
      if (state_->source_pos_error() < min_err) {
        min_err = state_->source_pos_error();
        mix_count_of_min_err = mix_count;
      }
      auto abs_source_pos_error = zx::duration(std::abs(state_->source_pos_error().get()));
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
        max_settled_err = std::max(state_->source_pos_error(), max_settled_err);
        min_settled_err = std::min(state_->source_pos_error(), min_settled_err);
      }

      if constexpr (kTraceClockSyncConvergence) {
        FX_LOGS(INFO) << "Testing " << rate_adjust_ppm << " PPM: [" << std::right << std::setw(3)
                      << mix_count << "], error " << std::setw(5)
                      << state_->source_pos_error().get();
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
          << " took too long to settle to 1% of initial worst-case desync "
          << one_percent_err_.get() << " nanosec: actual [" << actual_mix_count_one_percent_err
          << "] mixes, limit [" << limit_mix_count_one_percent_err_ << "] mixes";
    }

    EXPECT_LE(max_settled_err.get(), limit_settled_err_.get()) << "rate ppm " << rate_adjust_ppm;
    EXPECT_GE(min_settled_err.get(), -(limit_settled_err_.get())) << "rate ppm " << rate_adjust_ppm;

    if constexpr (kDisplayForPidCoefficientsTuning) {
      if (rate_adjust_ppm != 0) {
        FX_LOGS(INFO)
            << "****************************************************************************";
        if (zx::duration(std::abs(lower_limit_source_pos_err_.get())) >
            upper_limit_source_pos_err_) {
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
                      << std::setw(2) << actual_mix_count_one_percent_err + 1 << "] ("
                      << std::setw(2) << limit_mix_count_one_percent_err_ << " limit) to 1%   ("
                      << std::setw(3) << one_percent_err_.get() << ")";
        FX_LOGS(INFO) << "Final-settled [" << std::setw(2) << actual_mix_count_settled << "] ("
                      << std::setw(2) << limit_mix_count_settled_ << " limit) to "
                      << max_settled_err.get() << "/" << std::setw(2) << min_settled_err.get()
                      << " (" << limit_settled_err_.get() << " limit)";
      }
    }
  }

  zx::duration PrimaryErrorLimit(int32_t rate_adjust_ppm) {
    return zx::duration(static_cast<zx_duration_t>(static_cast<float>(rate_adjust_ppm) *
                                                   primary_err_ppm_multiplier_));
  }
  zx::duration SecondaryErrorLimit(int32_t rate_adjust_ppm) {
    return zx::duration(static_cast<zx_duration_t>(static_cast<float>(rate_adjust_ppm) *
                                                   secondary_err_ppm_multiplier_));
  }

  TimelineFunction RefToMonoTransform(zx::time start_time, int32_t rate_adjust_ppm) {
    return TimelineFunction(clock_realm_->now().get(), start_time.get(), 1'000'000,
                            1'000'000 + rate_adjust_ppm);
  }

  TimelineFunction RefToMonoTransform(const zx::clock& clock) {
    zx_clock_details_v1_t clock_details;
    clock.get_details(&clock_details);

    const auto offset = clock_details.mono_to_synthetic.synthetic_offset -
                        clock_details.mono_to_synthetic.reference_offset;
    return TimelineFunction(clock_realm_->now().get(), clock_realm_->now().get() + offset,
                            clock_details.mono_to_synthetic.rate.reference_ticks,
                            clock_details.mono_to_synthetic.rate.synthetic_ticks);
  }

  std::shared_ptr<MixerSource> mixer_source_;
  const Sampler::State* state_;

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
  zx::duration one_percent_err_;    // 1% of the maximum allowed primary error.
  zx::duration limit_settled_err_;  // Largest err allowed during final `kMixCountSettled` mixes.

  Direction direction_;  // Data flow of client->device (`kRender`) or device->client (`kCapture`).

  std::shared_ptr<SyntheticClockRealm> clock_realm_ = SyntheticClockRealm::Create();

  TimelineFunction device_ref_to_frac_frames_;
  TimelineFunction client_ref_to_frac_frames_;
};

// `MicroSrcTest` uses a custom client clock, with a default non-adjustable device clock. This
// combination forces `MixerSource` to use micro-SRC to reconcile any rate differences.
class MicroSrcTest : public MixerSourceClockTest, public testing::WithParamInterface<Direction> {
 protected:
  void SetUp() override { direction_ = GetParam(); }

  void SetRateLimits(int32_t rate_adjust_ppm) override {
    primary_err_ppm_multiplier_ = kMicroSrcPrimaryErrPpmMultiplier;
    secondary_err_ppm_multiplier_ = kMicroSrcSecondaryErrPpmMultiplier;

    limit_mix_count_settled_ = kMicroSrcMixCountUntilSettled;
    total_mix_count_ = limit_mix_count_settled_ + kMicroSrcMixCountSettledVerificationPeriod;

    limit_mix_count_one_usec_err_ = kMicroSrcLimitMixCountOneUsecErr;
    limit_mix_count_one_percent_err_ = kMicroSrcLimitMixCountOnePercentErr;

    limit_settled_err_ = kMicroSrcLimitSettledErr;

    MixerSourceClockTest::SetRateLimits(rate_adjust_ppm);
  }

  // Establishes reference clocks and ref-clock-to-frac-frame transforms for both client and device,
  // depending on which synchronization mode is being tested.
  void SetClocks(ClockMode clock_mode, int32_t rate_adjust_ppm) override {
    device_ref_to_frac_frames_ = TimelineFunction(
        0, clock_realm_->now().get(), Fixed(kDefaultFormat.frames_per_second()).raw_value(),
        zx::sec(1).to_nsecs());

    device_clock_ = clock_realm_->CreateClock("synthetic_device_fixed", Clock::kMonotonicDomain,
                                              false, RefToMonoTransform(clock::CloneOfMonotonic()));
    {
      SCOPED_TRACE("device clock must advance");
      clock::testing::VerifyAdvances(*device_clock_, *clock_realm_);
    }

    zx::time source_start = clock_realm_->now();
    if (clock_mode == ClockMode::kWithOffset) {
      source_start += kClockOffset;
    }

    client_ref_to_frac_frames_ = TimelineFunction(
        0, source_start.get(), Fixed(kDefaultFormat.frames_per_second()).raw_value(),
        zx::sec(1).to_nsecs());

    client_clock_ = clock_realm_->CreateClock(
        "synthetic_client_fixed", Clock::kExternalDomain, false,
        RefToMonoTransform(source_start,
                           clock_mode == ClockMode::kRateAdjust ? rate_adjust_ppm : 0));
    {
      SCOPED_TRACE("client clock must advance");
      clock::testing::VerifyAdvances(*client_clock_, *clock_realm_);
    }
  }

 private:
  static constexpr zx::duration kClockOffset = zx::sec(42);
};

// `AdjustableClockTest` uses an adjustable client clock along with a non-adjustable device clock.
// `MixerSource` should adjust the adjustable clock to reconcile any rate differences.
class AdjustableClockTest : public MixerSourceClockTest,
                            public ::testing::WithParamInterface<Direction> {
 protected:
  void SetUp() override { direction_ = GetParam(); }

  void SetRateLimits(int32_t rate_adjust_ppm) override {
    primary_err_ppm_multiplier_ = kAdjustablePrimaryErrPpmMultiplier;
    secondary_err_ppm_multiplier_ = kAdjustableSecondaryErrPpmMultiplier;

    limit_mix_count_settled_ = kAdjustableMixCountUntilSettled;
    total_mix_count_ = limit_mix_count_settled_ + kAdjustableMixCountSettledVerificationPeriod;

    limit_mix_count_one_usec_err_ = kAdjustableLimitMixCountOneUsecErr;
    limit_mix_count_one_percent_err_ = kAdjustableLimitMixCountOnePercentErr;

    limit_settled_err_ = kAdjustableLimitSettledErr;

    MixerSourceClockTest::SetRateLimits(rate_adjust_ppm);
  }

  // Establishes reference clocks and ref-clock-to-frac-frame transforms for both client and device,
  // depending on which synchronization mode is being tested.
  void SetClocks(ClockMode clock_mode, int32_t rate_adjust_ppm) override {
    client_ref_to_frac_frames_ = TimelineFunction(
        0, clock_realm_->now().get(), Fixed(kDefaultFormat.frames_per_second()).raw_value(),
        zx::sec(1).to_nsecs());

    client_clock_ =
        clock_realm_->CreateClock("synthetic_client_adjustable", Clock::kExternalDomain, true,
                                  RefToMonoTransform(clock::AdjustableCloneOfMonotonic()));
    {
      SCOPED_TRACE("client clock must advance");
      clock::testing::VerifyAdvances(*client_clock_, *clock_realm_);
    }

    auto device_start = clock_realm_->now();
    if (clock_mode == ClockMode::kWithOffset) {
      device_start += kClockOffset;
    }

    device_ref_to_frac_frames_ = TimelineFunction(
        0, device_start.get(), Fixed(kDefaultFormat.frames_per_second()).raw_value(),
        zx::sec(1).to_nsecs());

    device_clock_ = clock_realm_->CreateClock(
        "synthetic_client_fixed", kNonMonotonicDomain, false,
        RefToMonoTransform(device_start,
                           clock_mode == ClockMode::kRateAdjust ? rate_adjust_ppm : 0));
    {
      SCOPED_TRACE("device clock must advance");
      clock::testing::VerifyAdvances(*device_clock_, *clock_realm_);
    }
  }

 private:
  static constexpr uint32_t kNonMonotonicDomain = 42;
  static constexpr zx::duration kClockOffset = zx::sec(68);
};

// `RevertToMonoTest` uses an adjustable clock that has been tuned away from 0ppm, with a  monotonic
// device clock. `MixerSource` should adjust the adjustable clock linearly, to reconcile
// rate/position differences with the monotonic clock as rapidly as possible.
class RevertToMonoTest : public MixerSourceClockTest,
                         public ::testing::WithParamInterface<Direction> {
 protected:
  void SetUp() override { direction_ = GetParam(); }

  void SetRateLimits(int32_t rate_adjust_ppm) override {
    primary_err_ppm_multiplier_ = kRevertToMonoPrimaryErrPpmMultiplier;
    secondary_err_ppm_multiplier_ = kRevertToMonoSecondaryErrPpmMultiplier;

    limit_mix_count_settled_ = kRevertToMonoMixCountUntilSettled;
    total_mix_count_ = limit_mix_count_settled_ + kRevertToMonoMixCountSettledVerificationPeriod;

    limit_mix_count_one_usec_err_ = kRevertToMonoLimitMixCountOneUsecErr;
    limit_mix_count_one_percent_err_ = kRevertToMonoLimitMixCountOnePercentErr;

    limit_settled_err_ = kRevertToMonoLimitSettledErr;

    MixerSourceClockTest::SetRateLimits(rate_adjust_ppm);
  }

  // To test RevertSourceToMonotonic/RevertDestToMonotonic clock sync modes, we use an adjustable
  // client clock, with a device clock in the monotonic domain. To test the clock when it must
  // adjust upward by rate_adjust_ppm, we initially set it too low (note -rate_adjust_ppm below).
  void SetClocks(ClockMode clock_mode, int32_t rate_adjust_ppm) override {
    client_ref_to_frac_frames_ = TimelineFunction(
        0, clock_realm_->now().get(), Fixed(kDefaultFormat.frames_per_second()).raw_value(),
        zx::sec(1).to_nsecs());

    zx::clock::update_args args;
    args.reset().set_rate_adjust(-rate_adjust_ppm);
    zx::clock adjusted_clock = clock::AdjustableCloneOfMonotonic();
    EXPECT_EQ(adjusted_clock.update(args), ZX_OK);

    client_clock_ = clock_realm_->CreateClock("synthetic_client_adjustable", Clock::kExternalDomain,
                                              true, RefToMonoTransform(adjusted_clock));
    {
      SCOPED_TRACE("client clock must advance");
      clock::testing::VerifyAdvances(*client_clock_, *clock_realm_);
    }

    auto device_start = clock_realm_->now();
    if (clock_mode == ClockMode::kWithOffset) {
      device_start += kClockOffset;
    }

    device_ref_to_frac_frames_ = TimelineFunction(
        0, device_start.get(), Fixed(kDefaultFormat.frames_per_second()).raw_value(),
        zx::sec(1).to_nsecs());

    device_clock_ = clock_realm_->CreateClock("synthetic_device_fixed", Clock::kMonotonicDomain,
                                              false, RefToMonoTransform(device_start, 0));
    {
      SCOPED_TRACE("device clock must advance");
      clock::testing::VerifyAdvances(*device_clock_, *clock_realm_);
    }
  }

 private:
  static constexpr zx::duration kClockOffset = zx::sec(243);
};

// MicroSrc sync mode does not rate-adjust a zx::clock, whereas AdjustSource|DestClock and
// RevertSource|DestToMonotonic modes do. Zircon clocks cannot adjust beyond [-1000, +1000] PPM,
// hindering our ability to chase device clocks running close to that limit. This is why
// MicroSrcTest tests "Up1000" and "Down1000", while AdjustableClockTest and RevertToMonoTest
// use a reasonable validation outer limit of 750 PPM.

// Micro-SRC tests.
TEST_P(MicroSrcTest, Basic) { RunSyncTest(ClockMode::kSame); }
TEST_P(MicroSrcTest, Offset) { RunSyncTest(ClockMode::kWithOffset); }

TEST_P(MicroSrcTest, AdjustUp1) { RunSyncTest(ClockMode::kRateAdjust, 1); }
TEST_P(MicroSrcTest, AdjustDown1) { RunSyncTest(ClockMode::kRateAdjust, -1); }

TEST_P(MicroSrcTest, AdjustUp2) { RunSyncTest(ClockMode::kRateAdjust, 2); }
TEST_P(MicroSrcTest, AdjustDown2) { RunSyncTest(ClockMode::kRateAdjust, -2); }

TEST_P(MicroSrcTest, AdjustUp3) { RunSyncTest(ClockMode::kRateAdjust, 3); }
TEST_P(MicroSrcTest, AdjustDown3) { RunSyncTest(ClockMode::kRateAdjust, -3); }

TEST_P(MicroSrcTest, AdjustUp10) { RunSyncTest(ClockMode::kRateAdjust, 10); }
TEST_P(MicroSrcTest, AdjustDown10) { RunSyncTest(ClockMode::kRateAdjust, -10); }

TEST_P(MicroSrcTest, AdjustUp30) { RunSyncTest(ClockMode::kRateAdjust, 30); }
TEST_P(MicroSrcTest, AdjustDown30) { RunSyncTest(ClockMode::kRateAdjust, -30); }

TEST_P(MicroSrcTest, AdjustUp100) { RunSyncTest(ClockMode::kRateAdjust, 100); }
TEST_P(MicroSrcTest, AdjustDown100) { RunSyncTest(ClockMode::kRateAdjust, -100); }

TEST_P(MicroSrcTest, AdjustUp300) { RunSyncTest(ClockMode::kRateAdjust, 300); }
TEST_P(MicroSrcTest, AdjustDown300) { RunSyncTest(ClockMode::kRateAdjust, -300); }

TEST_P(MicroSrcTest, AdjustUp1000) { RunSyncTest(ClockMode::kRateAdjust, 1000); }
TEST_P(MicroSrcTest, AdjustDown1000) { RunSyncTest(ClockMode::kRateAdjust, -1000); }

// Adjustable clock tests.
TEST_P(AdjustableClockTest, Basic) { RunSyncTest(ClockMode::kSame); }
TEST_P(AdjustableClockTest, Offset) { RunSyncTest(ClockMode::kWithOffset); }

TEST_P(AdjustableClockTest, AdjustUp1) { RunSyncTest(ClockMode::kRateAdjust, 1); }
TEST_P(AdjustableClockTest, AdjustDown1) { RunSyncTest(ClockMode::kRateAdjust, -1); }

TEST_P(AdjustableClockTest, AdjustUp2) { RunSyncTest(ClockMode::kRateAdjust, 2); }
TEST_P(AdjustableClockTest, AdjustDown2) { RunSyncTest(ClockMode::kRateAdjust, -2); }

TEST_P(AdjustableClockTest, AdjustUp3) { RunSyncTest(ClockMode::kRateAdjust, 3); }
TEST_P(AdjustableClockTest, AdjustDown3) { RunSyncTest(ClockMode::kRateAdjust, -3); }

TEST_P(AdjustableClockTest, AdjustUp10) { RunSyncTest(ClockMode::kRateAdjust, 10); }
TEST_P(AdjustableClockTest, AdjustDown10) { RunSyncTest(ClockMode::kRateAdjust, -10); }

TEST_P(AdjustableClockTest, AdjustUp30) { RunSyncTest(ClockMode::kRateAdjust, 30); }
TEST_P(AdjustableClockTest, AdjustDown30) { RunSyncTest(ClockMode::kRateAdjust, -30); }

TEST_P(AdjustableClockTest, AdjustUp100) { RunSyncTest(ClockMode::kRateAdjust, 100); }
TEST_P(AdjustableClockTest, AdjustDown100) { RunSyncTest(ClockMode::kRateAdjust, -100); }

TEST_P(AdjustableClockTest, AdjustUp300) { RunSyncTest(ClockMode::kRateAdjust, 300); }
TEST_P(AdjustableClockTest, AdjustDown300) { RunSyncTest(ClockMode::kRateAdjust, -300); }

TEST_P(AdjustableClockTest, AdjustUp750) { RunSyncTest(ClockMode::kRateAdjust, 750); }
TEST_P(AdjustableClockTest, AdjustDown750) { RunSyncTest(ClockMode::kRateAdjust, -750); }

// Revert to monotonic clock tests.
TEST_P(RevertToMonoTest, Basic) { RunSyncTest(ClockMode::kSame); }
TEST_P(RevertToMonoTest, Offset) { RunSyncTest(ClockMode::kWithOffset); }

TEST_P(RevertToMonoTest, AdjustUp1) { RunSyncTest(ClockMode::kRateAdjust, 1); }
TEST_P(RevertToMonoTest, AdjustDown1) { RunSyncTest(ClockMode::kRateAdjust, -1); }

TEST_P(RevertToMonoTest, AdjustUp2) { RunSyncTest(ClockMode::kRateAdjust, 2); }
TEST_P(RevertToMonoTest, AdjustDown2) { RunSyncTest(ClockMode::kRateAdjust, -2); }

TEST_P(RevertToMonoTest, AdjustUp3) { RunSyncTest(ClockMode::kRateAdjust, 3); }
TEST_P(RevertToMonoTest, AdjustDown3) { RunSyncTest(ClockMode::kRateAdjust, -3); }

TEST_P(RevertToMonoTest, AdjustUp10) { RunSyncTest(ClockMode::kRateAdjust, 10); }
TEST_P(RevertToMonoTest, AdjustDown10) { RunSyncTest(ClockMode::kRateAdjust, -10); }

TEST_P(RevertToMonoTest, AdjustUp30) { RunSyncTest(ClockMode::kRateAdjust, 30); }
TEST_P(RevertToMonoTest, AdjustDown30) { RunSyncTest(ClockMode::kRateAdjust, -30); }

TEST_P(RevertToMonoTest, AdjustUp100) { RunSyncTest(ClockMode::kRateAdjust, 100); }
TEST_P(RevertToMonoTest, AdjustDown100) { RunSyncTest(ClockMode::kRateAdjust, -100); }

TEST_P(RevertToMonoTest, AdjustUp300) { RunSyncTest(ClockMode::kRateAdjust, 300); }
TEST_P(RevertToMonoTest, AdjustDown300) { RunSyncTest(ClockMode::kRateAdjust, -300); }

TEST_P(RevertToMonoTest, AdjustUp750) { RunSyncTest(ClockMode::kRateAdjust, 750); }
TEST_P(RevertToMonoTest, AdjustDown750) { RunSyncTest(ClockMode::kRateAdjust, -750); }

// Test subclasses are parameterized to run in render and capture directions.
// Thus every clock type is tested as a source and as a destination.
template <typename TestClass>
std::string PrintDirectionParam(const testing::TestParamInfo<typename TestClass::ParamType>& info) {
  return (info.param == Direction::kRender ? "Render" : "Capture");
}

#define INSTANTIATE_SYNC_TEST_SUITE(_test_class_name)                                \
  INSTANTIATE_TEST_SUITE_P(ClockSync, _test_class_name,                              \
                           testing::Values(Direction::kRender, Direction::kCapture), \
                           PrintDirectionParam<_test_class_name>)

INSTANTIATE_SYNC_TEST_SUITE(MicroSrcTest);
INSTANTIATE_SYNC_TEST_SUITE(AdjustableClockTest);
INSTANTIATE_SYNC_TEST_SUITE(RevertToMonoTest);

}  // namespace
}  // namespace media_audio
