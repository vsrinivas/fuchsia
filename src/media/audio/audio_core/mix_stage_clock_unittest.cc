// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include <fbl/string_printf.h>
#include <gmock/gmock.h>

#include "src/media/audio/audio_core/audio_clock.h"
#include "src/media/audio/audio_core/mix_stage.h"
#include "src/media/audio/audio_core/mixer/gain.h"
#include "src/media/audio/audio_core/packet_queue.h"
#include "src/media/audio/audio_core/ring_buffer.h"
#include "src/media/audio/audio_core/testing/fake_stream.h"
#include "src/media/audio/audio_core/testing/packet_factory.h"
#include "src/media/audio/audio_core/testing/threading_model_fixture.h"
#include "src/media/audio/lib/clock/clone_mono.h"
#include "src/media/audio/lib/clock/testing/clock_test.h"

using testing::Each;
using testing::FloatEq;

namespace media::audio {
namespace {

enum class ClockMode { SAME, WITH_OFFSET, RATE_ADJUST };

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
// MixStageClockTest (MicroSrcClockTest)
//
// This set of tests validates how MixStage handles clock synchronization
//
// Currently, we tune PIDs by running all of these test cases.
// Most recent tuning occurred 09/28/2020 with Fixed frames defined to have 13 fractional bits.
//
// There are three synchronization scenarios to be validated:
//  1) Client and device clocks are non-adjustable -- apply micro-SRC (MicroSrcClockTest)
//  2) Client clock is adjustable -- tune this adjustable client clock (not yet implemented)
//  3) Device clock is adjustable -- trim the hardware clock (not yet implemented).
//

// With any error detection and adaptive convergence, the initial (primary) error is often
// followed by a smaller "correction overshoot" (secondary) error of opposite magnitude.
//
// Current worst-case position error deviation, based on current PID coefficients:
//                           Major (immediate response)          Minor (overshoot)
// Worst-case error:         ~8 frac-frames-per-ppm-change       ~1 frac-frames-per-ppm-change
// Occurring after:          5000-6000 frames                    9000 frames
// Translated to 48kHz:      20nsec-per-ppm, after ~115ms        2.5 nsec-per-ppm, after ~190ms
//
// Thus in the absolute worst-case scenario, a rate change of 2000ppm (from -1000 adjusted, to
// +1000 adjusted) should cause worst-case desync position error of less than 16000 sub-frames
// (< 2 frame) -- about 40 microsec at 48kHz.
//
// Note: these are subject to change as we tune the PID coefficients for best performance.
//

// These multipliers (scaled by rate_adjust_ppm) determine worst-case primary/secondary error
// limits. Error is calculated by: taking the Actual long-running source position (maintained from
// the amount advanced in each Mix call) and subtracting the Expected source position (calculated by
// converting dest frame through dest and source clocks to fractional source). Thus if our Expected
// (clock-derived) source position is too high, we calculate a NEGATIVE position error.
//
static constexpr float kMicroSrcPrimaryErrPpmMultiplier = -7.1;  // positive error? consume slower

static constexpr float kMicroSrcSecondaryErrPpmMultiplier = 1;

static constexpr int32_t kMicroSrcMixCountUntilSettled = 100;

static constexpr int32_t kMicroSrcMixCountSettledVerificationPeriod = 10;

static constexpr Fixed kMicroSrcLimitSettledErr = Fixed::FromRaw(2);

static constexpr int32_t kMicroSrcLimitMixCountOneUsecErr = 30;

static constexpr int32_t kMicroSrcLimitMixCountOnePercentErr = 45;

static constexpr Fixed kOneUsecErr = Fixed(kDefaultFrameRate) / 1'000'000;

// When tuning a new set of PID coefficients, set this to enable additional logging.
constexpr bool kDisplayForPidCoefficientsTuning = true;
constexpr bool kTraceNewPidCoefficientsTuning = false;

class MixStageClockTest : public testing::ThreadingModelFixture {
 protected:
  // We will measure long-running position across mixes of 10ms (our block size).
  // TODO(fxbug.dev/56635): If our mix timeslice shortens, adjust the below and retune the PIDs.
  static constexpr zx::duration kClockSyncMixDuration = zx::msec(10);
  static constexpr uint32_t kFramesToMix =
      kDefaultFrameRate * kClockSyncMixDuration.to_msecs() / 1000;

  fbl::RefPtr<VersionedTimelineFunction> timeline_function_ =
      fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(TimelineRate(
          Fixed(kDefaultFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));

  virtual void SetRateLimits(int32_t rate_adjust_ppm);
  virtual void SetUpClocks(ClockMode clock_mode, zx::clock raw_clock) = 0;
  virtual Fixed PrimaryErrorLimit(int32_t rate_adjust_ppm) = 0;
  virtual Fixed SecondaryErrorLimit(int32_t rate_adjust_ppm) = 0;

  void VerifySynchronization(ClockMode clock_mode = ClockMode::SAME, int32_t rate_adjust_ppm = 0);
  void SyncTest(int32_t rate_adjust_ppm);

  std::shared_ptr<MixStage> mix_stage_;
  std::shared_ptr<Mixer> mixer_;

  std::optional<AudioClock> client_clock_;
  std::optional<AudioClock> device_clock_;

  bool wait_for_mixes_;  // Do we wait for MONOTONIC time to pass between mixes.

  int32_t total_mix_count_;
  int32_t limit_mix_count_settled_;
  int32_t limit_mix_count_one_usec_err_;
  int32_t limit_mix_count_one_percent_err_;

  Fixed upper_limit_src_pos_err_;
  Fixed lower_limit_src_pos_err_;

  Fixed one_usec_err_;       // At default rate, 1 microsecond expressed in fractional frames.
  Fixed one_percent_err_;    // 1% of the maximum allowed primary error
  Fixed limit_settled_err_;  // Largest error allowed during the final kMixCountSettled mixes.
};

// MicroSrcClockTest uses a custom client clock, with a default non-adjustable device clock. This
// combination forces AudioCore to use "micro-SRC" to reconcile any rate differences.
class MicroSrcClockTest : public MixStageClockTest {
 protected:
  void SetRateLimits(int32_t rate_adjust_ppm) override {
    wait_for_mixes_ = false;  // no zx::clock rate_adjust usage: runs faster than real-time

    limit_mix_count_settled_ = kMicroSrcMixCountUntilSettled;
    total_mix_count_ = limit_mix_count_settled_ + kMicroSrcMixCountSettledVerificationPeriod;

    limit_mix_count_one_usec_err_ = kMicroSrcLimitMixCountOneUsecErr;
    limit_mix_count_one_percent_err_ = kMicroSrcLimitMixCountOnePercentErr;

    limit_settled_err_ = kMicroSrcLimitSettledErr;

    MixStageClockTest::SetRateLimits(rate_adjust_ppm);
  }

  void SetUpClocks(ClockMode clock_mode, zx::clock raw_clock) {
    client_clock_ = AudioClock::CreateAsClientNonadjustable(std::move(raw_clock));

    device_clock_ = AudioClock::CreateAsDeviceNonadjustable(clock::CloneOfMonotonic(),
                                                            AudioClock::kMonotonicDomain);
  }

  Fixed PrimaryErrorLimit(int32_t rate_adjust_ppm) override {
    return Fixed::FromRaw(rate_adjust_ppm * kMicroSrcPrimaryErrPpmMultiplier);
  }
  Fixed SecondaryErrorLimit(int32_t rate_adjust_ppm) override {
    return Fixed::FromRaw(rate_adjust_ppm * kMicroSrcSecondaryErrPpmMultiplier);
  }
};

// Set the limits for worst-case source position error during this mix interval
//
void MixStageClockTest::SetRateLimits(int32_t rate_adjust_ppm) {
  // Set the limits for worst-case source position error during this mix interval
  // If clock runs fast, our initial error is negative (position too low), followed by smaller
  // positive error (position too high). These are reversed if clock runs slow.
  auto primary_err_limit = PrimaryErrorLimit(rate_adjust_ppm);
  auto secondary_err_limit = SecondaryErrorLimit(rate_adjust_ppm);

  // Our maximum positive and negative error values are determined by the magnitude of the rate
  // adjustment. At very small rate_adjust_ppm, these values can be overshadowed by any steady-state
  // "ripple" we might have, so include that "ripple" value in our max/min and 1% errors.
  auto min_max = std::minmax(primary_err_limit, secondary_err_limit);
  lower_limit_src_pos_err_ = min_max.first - limit_settled_err_;
  upper_limit_src_pos_err_ = min_max.second + limit_settled_err_;

  one_usec_err_ = std::max(limit_settled_err_, kOneUsecErr);
  Fixed primary_err_one_percent = primary_err_limit.Absolute() / 100;
  one_percent_err_ = primary_err_one_percent + limit_settled_err_;

  limit_mix_count_one_usec_err_ = std::min(limit_mix_count_one_usec_err_, limit_mix_count_settled_);
  limit_mix_count_one_percent_err_ =
      std::min(limit_mix_count_one_percent_err_, limit_mix_count_settled_);
}

// Set up the various prerequisites of a clock synchronization test, then execute the test.
void MixStageClockTest::VerifySynchronization(ClockMode clock_mode, int32_t rate_adjust_ppm) {
  SetRateLimits(rate_adjust_ppm);

  // Set properties for the requested clock, and create it.
  clock::testing::ClockProperties clock_props;
  if (clock_mode == ClockMode::WITH_OFFSET) {
    clock_props = {.start_val = zx::clock::get_monotonic() - zx::sec(3)};
  } else if (clock_mode == ClockMode::RATE_ADJUST) {
    clock_props = {.start_val = zx::time(0), .rate_adjust_ppm = rate_adjust_ppm};
  }
  auto raw_clock = clock::testing::CreateCustomClock(clock_props).take_value();

  // Call subclass-specific method to set up clocks for that synchronization method.
  SetUpClocks(clock_mode, std::move(raw_clock));

  // Create our source format transform; pass it and the client clock to a packet queue
  auto nsec_to_frac_src = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
      TimelineRate(Fixed(kDefaultFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));
  std::shared_ptr<PacketQueue> packet_queue = std::make_shared<PacketQueue>(
      kDefaultFormat, nsec_to_frac_src, std::move(client_clock_.value()));

  // Pass the dest transform and device clock to a mix stage
  mix_stage_ = std::make_shared<MixStage>(kDefaultFormat, kFramesToMix, timeline_function_,
                                          device_clock_.value());

  // Connect packet queue to mix stage; we inspect running position & error via Mixer::SourceInfo.
  mixer_ = mix_stage_->AddInput(packet_queue);

  SyncTest(rate_adjust_ppm);
}

// Test the accuracy of long-running position maintained by MixStage across ReadLock calls. No
// source audio is needed: source position is determined by clocks and the change in dest position.
//
// Because a feedback control is used to resolve rate adjustment, we run the mix for a significant
// interval, measuring worst-case source position error. We separately note the worst-case source
// position error during the final few mixes. The overall worst-case error observed should be
// proportional to the magnitude of rate change, whereas once we settle to steady state our position
// desync error should have a ripple of much less than 1 usec.
//
void MixStageClockTest::SyncTest(int32_t rate_adjust_ppm) {
  auto& mix_info = mixer_->source_info();

  // We measure worst-case desync (both ahead and behind [positive and negative]), as well as
  // "desync ripple" measured during the final 10 mixes, after synch should have converged.
  Fixed max_err{0}, max_settled_err{0};
  Fixed min_err{0}, min_settled_err{0};
  int32_t mix_count_of_max_err = -1, mix_count_of_min_err = -1;
  int32_t actual_mix_count_one_percent_err = -1, actual_mix_count_one_usec_err = -1;
  int32_t actual_mix_count_settled = -1;

  auto mono_start = zx::clock::get_monotonic();
  for (auto mix_count = 0; mix_count < total_mix_count_; ++mix_count) {
    if (wait_for_mixes_) {
      zx::nanosleep(mono_start + kClockSyncMixDuration * mix_count);
    }
    mix_stage_->ReadLock(Fixed(kFramesToMix * mix_count), kFramesToMix);
    ASSERT_EQ(mix_info.next_dest_frame, kFramesToMix * (mix_count + 1));

    // Track the worst-case position errors (overall min/max, 1%, 1us, final-settled).
    if (mix_info.frac_source_error > max_err) {
      max_err = mix_info.frac_source_error;
      mix_count_of_max_err = mix_count;
    }
    if (mix_info.frac_source_error < min_err) {
      min_err = mix_info.frac_source_error;
      mix_count_of_min_err = mix_count;
    }
    if (mix_info.frac_source_error.Absolute() > one_percent_err_) {
      actual_mix_count_one_percent_err = mix_count;
    }
    if (mix_info.frac_source_error.Absolute() > one_usec_err_) {
      actual_mix_count_one_usec_err = mix_count;
    }
    if (mix_info.frac_source_error.Absolute() > limit_settled_err_) {
      actual_mix_count_settled = mix_count;
    }

    if (mix_count >= limit_mix_count_settled_) {
      max_settled_err = std::max(mix_info.frac_source_error, max_settled_err);
      min_settled_err = std::min(mix_info.frac_source_error, min_settled_err);
    }

    if constexpr (kTraceNewPidCoefficientsTuning) {
      FX_LOGS(INFO) << std::setw(5) << rate_adjust_ppm << ": [" << std::right << std::setw(3)
                    << mix_count << "], error " << std::setw(5)
                    << mix_info.frac_source_error.raw_value();
    }
  }

  EXPECT_LE(max_err.raw_value(), upper_limit_src_pos_err_.raw_value())
      << "rate ppm " << rate_adjust_ppm << " at mix_count[" << mix_count_of_max_err << "] "
      << mix_count_of_max_err * kClockSyncMixDuration.to_msecs() << "ms";
  EXPECT_GE(min_err.raw_value(), lower_limit_src_pos_err_.raw_value())
      << "rate ppm " << rate_adjust_ppm << " at mix_count[" << mix_count_of_min_err << "] "
      << mix_count_of_min_err * kClockSyncMixDuration.to_msecs() << "ms";

  if (rate_adjust_ppm != 0) {
    EXPECT_LE(actual_mix_count_one_usec_err, limit_mix_count_one_usec_err_)
        << "rate ppm " << rate_adjust_ppm << " took too long to settle to "
        << limit_settled_err_.raw_value() << " sub-frames desync (1 microsecond)";

    EXPECT_LE(actual_mix_count_one_percent_err, limit_mix_count_one_percent_err_)
        << "rate ppm " << rate_adjust_ppm
        << " took too long to settle to 1% of initial worst-case desync "
        << one_percent_err_.raw_value() << ": actual [" << actual_mix_count_one_percent_err
        << "], limit [" << limit_mix_count_one_percent_err_ << "]";

    EXPECT_LE(max_settled_err.raw_value(), limit_settled_err_.raw_value())
        << "rate ppm " << rate_adjust_ppm;
    EXPECT_GE(min_settled_err.raw_value(), -limit_settled_err_.raw_value())
        << "rate ppm " << rate_adjust_ppm;
  }

  if constexpr (kDisplayForPidCoefficientsTuning) {
    if (rate_adjust_ppm != 0) {
      FX_LOGS(INFO)
          << "****************************************************************************";
      if (lower_limit_src_pos_err_.Absolute() > upper_limit_src_pos_err_) {
        FX_LOGS(INFO)
            << fbl::StringPrintf(
                   "Rate %5d: Primary [%2d] %5ld (%5ld limit); Secondary [%2d] %4ld (%4ld limit)",
                   rate_adjust_ppm, mix_count_of_min_err, min_err.raw_value(),
                   lower_limit_src_pos_err_.raw_value(), mix_count_of_max_err, max_err.raw_value(),
                   upper_limit_src_pos_err_.raw_value())
                   .c_str();
      } else {
        FX_LOGS(INFO)
            << fbl::StringPrintf(
                   "Rate %5d: Primary [%2d] %5ld (%5ld limit); Secondary [%2d] %4ld (%4ld limit)",
                   rate_adjust_ppm, mix_count_of_max_err, max_err.raw_value(),
                   upper_limit_src_pos_err_.raw_value(), mix_count_of_min_err, min_err.raw_value(),
                   lower_limit_src_pos_err_.raw_value())
                   .c_str();
      }

      FX_LOGS(INFO) << (actual_mix_count_one_usec_err < limit_mix_count_one_usec_err_
                            ? "Converged by  ["
                            : "NOT converged [")
                    << std::setw(2) << actual_mix_count_one_usec_err + 1 << "] (" << std::setw(2)
                    << limit_mix_count_one_usec_err_ << " limit) to 1us  (" << std::setw(3)
                    << one_usec_err_.raw_value() << ")";
      FX_LOGS(INFO) << (actual_mix_count_one_percent_err < limit_mix_count_one_percent_err_
                            ? "Converged by  ["
                            : "NOT converged [")
                    << std::setw(2) << actual_mix_count_one_percent_err + 1 << "] (" << std::setw(2)
                    << limit_mix_count_one_percent_err_ << " limit) to 1%   (" << std::setw(3)
                    << one_percent_err_.raw_value() << ")";
      FX_LOGS(INFO) << "Final-settled [" << std::setw(2) << actual_mix_count_settled << "] ("
                    << std::setw(2) << limit_mix_count_settled_ << " limit) to "
                    << max_settled_err.raw_value() << "/" << std::setw(2)
                    << min_settled_err.raw_value() << " (" << limit_settled_err_.raw_value()
                    << " limit)";
    }
  }
}

// Test cases that validate the MixStage+AudioClock "micro-SRC" synchronization path.
TEST_F(MicroSrcClockTest, Basic) { VerifySynchronization(); }
TEST_F(MicroSrcClockTest, Offset) { VerifySynchronization(ClockMode::WITH_OFFSET); }

TEST_F(MicroSrcClockTest, AdjustUp1) { VerifySynchronization(ClockMode::RATE_ADJUST, 1); }
TEST_F(MicroSrcClockTest, AdjustDown1) { VerifySynchronization(ClockMode::RATE_ADJUST, -1); }

TEST_F(MicroSrcClockTest, AdjustUp2) { VerifySynchronization(ClockMode::RATE_ADJUST, 2); }
TEST_F(MicroSrcClockTest, AdjustDown2) { VerifySynchronization(ClockMode::RATE_ADJUST, -2); }

TEST_F(MicroSrcClockTest, AdjustUp3) { VerifySynchronization(ClockMode::RATE_ADJUST, 3); }
TEST_F(MicroSrcClockTest, AdjustDown3) { VerifySynchronization(ClockMode::RATE_ADJUST, -3); }

TEST_F(MicroSrcClockTest, AdjustUp10) { VerifySynchronization(ClockMode::RATE_ADJUST, 10); }
TEST_F(MicroSrcClockTest, AdjustDown10) { VerifySynchronization(ClockMode::RATE_ADJUST, -10); }

TEST_F(MicroSrcClockTest, AdjustUp30) { VerifySynchronization(ClockMode::RATE_ADJUST, 30); }
TEST_F(MicroSrcClockTest, AdjustDown30) { VerifySynchronization(ClockMode::RATE_ADJUST, -30); }

TEST_F(MicroSrcClockTest, AdjustUp100) { VerifySynchronization(ClockMode::RATE_ADJUST, 100); }
TEST_F(MicroSrcClockTest, AdjustDown100) { VerifySynchronization(ClockMode::RATE_ADJUST, -100); }

TEST_F(MicroSrcClockTest, AdjustUp300) { VerifySynchronization(ClockMode::RATE_ADJUST, 300); }
TEST_F(MicroSrcClockTest, AdjustDown300) { VerifySynchronization(ClockMode::RATE_ADJUST, -300); }

TEST_F(MicroSrcClockTest, AdjustUp1000) { VerifySynchronization(ClockMode::RATE_ADJUST, 1000); }
TEST_F(MicroSrcClockTest, AdjustDown1000) { VerifySynchronization(ClockMode::RATE_ADJUST, -1000); }

}  // namespace

}  // namespace media::audio
