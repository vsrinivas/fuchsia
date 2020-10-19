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
#include "src/media/audio/audio_core/testing/audio_clock_helper.h"
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
// MixStageClockTest (MicroSrcTest)
//
// This set of tests validates how MixStage handles clock synchronization
//
// Currently, we tune PIDs by running all of these test cases.
// Most recent tuning occurred 10/15/2020, having moved from frames/fixed-subframes to time units.
//
// There are three synchronization scenarios to be validated:
//  1) Client and device clocks are non-adjustable -- apply micro-SRC (MicroSrcTest)
//  2) Client clock is adjustable -- tune this adjustable client clock (not yet implemented)
//  3) Device clock is adjustable -- trim the hardware clock (not yet implemented).
//

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
static constexpr float kMicroSrcPrimaryErrPpmMultiplier = -10.01;  // positive err? consume slower

static constexpr float kMicroSrcSecondaryErrPpmMultiplier = 0.9;

static constexpr int32_t kMicroSrcMixCountUntilSettled = 15;

static constexpr int32_t kMicroSrcMixCountSettledVerificationPeriod = 1000;

static constexpr auto kMicroSrcLimitSettledErr = zx::duration(15);

static constexpr int32_t kMicroSrcLimitMixCountOneUsecErr = 4;

static constexpr int32_t kMicroSrcLimitMixCountOnePercentErr = 12;

// When tuning a new set of PID coefficients, set this to enable additional logging.
constexpr bool kDisplayForPidCoefficientsTuning = false;
constexpr bool kTraceClockSyncConvergence = false;

enum class Direction { Render, Capture };

class MixStageClockTest : public testing::ThreadingModelFixture {
 protected:
  // We measure long-running position across mixes of 10ms (our block size).
  // TODO(fxbug.dev/56635): If our mix timeslice shortens, adjust the below and retune the PIDs.
  static constexpr zx::duration kClockSyncMixDuration = zx::msec(10);
  static constexpr uint32_t kFramesToMix =
      kDefaultFrameRate * kClockSyncMixDuration.to_msecs() / 1000;

  fbl::RefPtr<VersionedTimelineFunction> device_ref_to_fixed_;
  fbl::RefPtr<VersionedTimelineFunction> client_ref_to_fixed_;

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

  std::optional<AudioClock> client_clock_;
  std::optional<AudioClock> device_clock_;

  bool wait_for_mixes_;  // Does this sync mode require MONOTONIC time to pass between mixes.

  int32_t total_mix_count_;
  int32_t limit_mix_count_settled_;
  int32_t limit_mix_count_one_usec_err_;
  int32_t limit_mix_count_one_percent_err_;

  float primary_err_ppm_multiplier_;
  float secondary_err_ppm_multiplier_;
  zx::duration upper_limit_src_pos_err_;
  zx::duration lower_limit_src_pos_err_;

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
    wait_for_mixes_ = false;  // no zx::clock rate_adjust usage: runs faster than real-time

    if (direction_ == Direction::Render) {
      primary_err_ppm_multiplier_ = kMicroSrcPrimaryErrPpmMultiplier;
      secondary_err_ppm_multiplier_ = kMicroSrcSecondaryErrPpmMultiplier;
    } else {
      primary_err_ppm_multiplier_ = -kMicroSrcPrimaryErrPpmMultiplier;
      secondary_err_ppm_multiplier_ = -kMicroSrcSecondaryErrPpmMultiplier;
    }

    limit_mix_count_settled_ = kMicroSrcMixCountUntilSettled;
    total_mix_count_ = limit_mix_count_settled_ + kMicroSrcMixCountSettledVerificationPeriod;

    limit_mix_count_one_usec_err_ = kMicroSrcLimitMixCountOneUsecErr;
    limit_mix_count_one_percent_err_ = kMicroSrcLimitMixCountOnePercentErr;

    limit_settled_err_ = kMicroSrcLimitSettledErr;

    MixStageClockTest::SetRateLimits(rate_adjust_ppm);
  }

  // Establish reference clocks and ref-clock-to-fixed-frame transforms for both client and device,
  // depending on which synchronization mode is being tested.
  void SetClocks(ClockMode clock_mode, int32_t rate_adjust_ppm) override {
    device_ref_to_fixed_ = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
        0, zx::clock::get_monotonic().get(), Fixed(kDefaultFormat.frames_per_second()).raw_value(),
        zx::sec(1).to_nsecs()));

    device_clock_ =
        AudioClock::DeviceFixed(clock::CloneOfMonotonic(), AudioClock::kMonotonicDomain);
    audio_clock_helper::VerifyAdvances(device_clock_.value());

    zx::time source_start = zx::clock::get_monotonic();
    clock::testing::ClockProperties clock_props;
    if (clock_mode == ClockMode::WITH_OFFSET) {
      source_start += kClockOffset;
      clock_props = {.start_val = source_start};
    } else if (clock_mode == ClockMode::RATE_ADJUST) {
      clock_props = {.rate_adjust_ppm = rate_adjust_ppm};
    }

    client_ref_to_fixed_ = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
        0, source_start.get(), Fixed(kDefaultFormat.frames_per_second()).raw_value(),
        zx::sec(1).to_nsecs()));

    auto raw_clock = clock::testing::CreateCustomClock(clock_props).take_value();
    client_clock_ = AudioClock::ClientFixed(std::move(raw_clock));
    audio_clock_helper::VerifyAdvances(client_clock_.value());
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

  // Max positive and negative error values are determined by the magnitude of rate adjustment. At
  // very small rate_adjust_ppm, these values can be overshadowed by any steady-state "ripple" we
  // might have, so include that "ripple" value in our max/min and 1% errors.
  auto min_max = std::minmax(primary_err_limit, secondary_err_limit);
  lower_limit_src_pos_err_ = min_max.first - limit_settled_err_;
  upper_limit_src_pos_err_ = min_max.second + limit_settled_err_;

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
    packet_queue = std::make_shared<PacketQueue>(kDefaultFormat, client_ref_to_fixed_,
                                                 std::move(client_clock_.value()));
    mix_stage_ = std::make_shared<MixStage>(kDefaultFormat, kFramesToMix, device_ref_to_fixed_,
                                            device_clock_.value());
  } else {
    // Create a PacketQueue with the device timeline and clock, as the source.
    // Pass the client timeline and clock to a mix stage, as the destination.
    packet_queue = std::make_shared<PacketQueue>(kDefaultFormat, device_ref_to_fixed_,
                                                 std::move(device_clock_.value()));
    mix_stage_ = std::make_shared<MixStage>(kDefaultFormat, kFramesToMix, client_ref_to_fixed_,
                                            client_clock_.value());
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
  auto& mix_info = mixer_->source_info();

  zx::duration max_err{0}, max_settled_err{0};
  zx::duration min_err{0}, min_settled_err{0};
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
    if (mix_info.src_pos_error > max_err) {
      max_err = mix_info.src_pos_error;
      mix_count_of_max_err = mix_count;
    }
    if (mix_info.src_pos_error < min_err) {
      min_err = mix_info.src_pos_error;
      mix_count_of_min_err = mix_count;
    }
    auto abs_src_pos_error = zx::duration(std::abs(mix_info.src_pos_error.get()));
    if (abs_src_pos_error > one_percent_err_) {
      actual_mix_count_one_percent_err = mix_count;
    }
    if (abs_src_pos_error > one_usec_err_) {
      actual_mix_count_one_usec_err = mix_count;
    }
    if (abs_src_pos_error > limit_settled_err_) {
      actual_mix_count_settled = mix_count;
    }

    if (mix_count >= limit_mix_count_settled_) {
      max_settled_err = std::max(mix_info.src_pos_error, max_settled_err);
      min_settled_err = std::min(mix_info.src_pos_error, min_settled_err);
    }

    if constexpr (kTraceClockSyncConvergence) {
      FX_LOGS(INFO) << std::setw(5) << rate_adjust_ppm << ": [" << std::right << std::setw(3)
                    << mix_count << "], error " << std::setw(5) << mix_info.src_pos_error.get();
    }
  }

  EXPECT_LE(max_err.get(), upper_limit_src_pos_err_.get())
      << "rate ppm " << rate_adjust_ppm << " at mix_count[" << mix_count_of_max_err << "] "
      << mix_count_of_max_err * kClockSyncMixDuration.to_msecs() << "ms";
  EXPECT_GE(min_err.get(), lower_limit_src_pos_err_.get())
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
      if (zx::duration(std::abs(lower_limit_src_pos_err_.get())) > upper_limit_src_pos_err_) {
        FX_LOGS(INFO)
            << fbl::StringPrintf(
                   "Rate %5d: Primary [%2d] %5ld (%5ld limit); Secondary [%2d] %5ld (%5ld limit)",
                   rate_adjust_ppm, mix_count_of_min_err, min_err.get(),
                   lower_limit_src_pos_err_.get(), mix_count_of_max_err, max_err.get(),
                   upper_limit_src_pos_err_.get())
                   .c_str();
      } else {
        FX_LOGS(INFO)
            << fbl::StringPrintf(
                   "Rate %5d: Primary [%2d] %5ld (%5ld limit); Secondary [%2d] %5ld (%5ld limit)",
                   rate_adjust_ppm, mix_count_of_max_err, max_err.get(),
                   upper_limit_src_pos_err_.get(), mix_count_of_min_err, min_err.get(),
                   lower_limit_src_pos_err_.get())
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

// Test cases that validate the MixStage+AudioClock "micro-SRC" synchronization path.
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

std::string PrintDirectionParam(const ::testing::TestParamInfo<MicroSrcTest::ParamType>& info) {
  return (info.param == Direction::Render ? "Render" : "Capture");
}

INSTANTIATE_TEST_SUITE_P(ClockSync, MicroSrcTest,
                         ::testing::Values(Direction::Render, Direction::Capture),
                         PrintDirectionParam);

}  // namespace

}  // namespace media::audio
