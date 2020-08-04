// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/utils.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <zircon/errors.h>

#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/clone_mono.h"

namespace media::audio::clock {
namespace {

TEST(ClockUtilsTest, DuplicateBadClock) {
  zx::clock uninitialized_clock;

  auto bad_result = DuplicateClock(uninitialized_clock);
  EXPECT_TRUE(bad_result.is_error());
}

// Immediately after duplication, the dupe clock has the same parameters
TEST(ClockUtilsTest, DuplicateClockIsIdentical) {
  zx::clock ref_clock;

  auto status =
      zx::clock::create(ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS, nullptr, &ref_clock);
  ASSERT_EQ(status, ZX_OK);

  auto result = DuplicateClock(ref_clock);
  EXPECT_TRUE(result.is_ok());
  zx::clock dupe_clock = result.take_value();
  EXPECT_TRUE(dupe_clock.is_valid());

  zx::clock::update_args args;
  args.reset().set_value(zx::time(123)).set_rate_adjust(-456);
  EXPECT_EQ(ref_clock.update(args), ZX_OK);

  zx_clock_details_v1_t clock_details, clock_details_dupe;
  EXPECT_EQ(ref_clock.get_details(&clock_details), ZX_OK);
  EXPECT_EQ(dupe_clock.get_details(&clock_details_dupe), ZX_OK);

  EXPECT_EQ(clock_details.options, clock_details_dupe.options);
  EXPECT_EQ(clock_details.last_value_update_ticks, clock_details_dupe.last_value_update_ticks);
  EXPECT_EQ(clock_details.last_rate_adjust_update_ticks,
            clock_details_dupe.last_rate_adjust_update_ticks);
  EXPECT_EQ(clock_details.generation_counter, clock_details_dupe.generation_counter);

  EXPECT_EQ(clock_details.mono_to_synthetic.reference_offset,
            clock_details_dupe.mono_to_synthetic.reference_offset);
  EXPECT_EQ(clock_details.mono_to_synthetic.synthetic_offset,
            clock_details_dupe.mono_to_synthetic.synthetic_offset);
  EXPECT_EQ(clock_details.mono_to_synthetic.rate.synthetic_ticks,
            clock_details_dupe.mono_to_synthetic.rate.synthetic_ticks);
  EXPECT_EQ(clock_details.mono_to_synthetic.rate.reference_ticks,
            clock_details_dupe.mono_to_synthetic.rate.reference_ticks);
}

// The duplicate clock can be read
TEST(ClockUtilsTest, DuplicateClockCanBeRead) {
  zx::clock ref_clock = audio::clock::CloneOfMonotonic();
  EXPECT_TRUE(ref_clock.is_valid());

  zx_time_t now;
  EXPECT_EQ(ref_clock.read(&now), ZX_OK);

  auto result = DuplicateClock(ref_clock);
  EXPECT_TRUE(result.is_ok());
  zx::clock dupe_clock = result.take_value();
  EXPECT_TRUE(dupe_clock.is_valid());

  zx_time_t now2;
  EXPECT_EQ(dupe_clock.read(&now2), ZX_OK);
  EXPECT_GT(now2, now);
}

// The duplicate clock should not be adjustable.
TEST(ClockUtilsTest, DuplicateClockCannotBeAdjusted) {
  zx::clock ref_clock;
  auto status =
      zx::clock::create(ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS, nullptr, &ref_clock);
  EXPECT_EQ(status, ZX_OK);

  // ref clock is not yet started
  zx_time_t now;
  EXPECT_EQ(ref_clock.read(&now), ZX_OK);
  EXPECT_EQ(now, 0);

  auto result = DuplicateClock(ref_clock);
  EXPECT_TRUE(result.is_ok());
  zx::clock dupe_clock = result.take_value();
  EXPECT_TRUE(dupe_clock.is_valid());

  zx::clock::update_args args;
  args.reset().set_value(zx::clock::get_monotonic());
  EXPECT_NE(dupe_clock.update(args), ZX_OK);

  // dupe is not yet started
  EXPECT_EQ(dupe_clock.read(&now), ZX_OK);
  EXPECT_EQ(now, 0);

  // ref can be updated
  EXPECT_EQ(ref_clock.update(args), ZX_OK);

  // dupe is now started
  EXPECT_EQ(dupe_clock.read(&now), ZX_OK);
  EXPECT_GT(now, 0);
}

// A duplicate clock can itself be further duplicated
TEST(ClockUtilsTest, DuplicateClockCanBeDuplicated) {
  zx::clock ref_clock = audio::clock::CloneOfMonotonic();
  EXPECT_TRUE(ref_clock.is_valid());

  auto result = DuplicateClock(ref_clock);
  EXPECT_TRUE(result.is_ok());
  zx::clock dupe_clock = result.take_value();
  EXPECT_TRUE(dupe_clock.is_valid());

  result = DuplicateClock(dupe_clock);
  EXPECT_TRUE(result.is_ok());
  zx::clock dupe_of_dupe_clock = result.take_value();
  EXPECT_TRUE(dupe_of_dupe_clock.is_valid());

  zx_time_t now;
  EXPECT_EQ(dupe_of_dupe_clock.read(&now), ZX_OK);
  EXPECT_GT(now, 0);
}

// With an uninitialized clock, GetAndDisplayClockDetails should not succeed
TEST(ClockUtilsTest, GetAndDisplayClockDetailsBadHandle) {
  zx::clock uninitialized_clock;

  auto result = GetClockDetails(uninitialized_clock);
  EXPECT_TRUE(result.is_error());
  EXPECT_EQ(result.error(), ZX_ERR_INVALID_ARGS);

  auto status = GetAndDisplayClockDetails(uninitialized_clock);
  EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
}

// SnapshotClock wraps clock::get_details and converts to a TimelineFunction
TEST(ClockUtilsTest, SnapshotClock) {
  zx::clock ref_clock;

  auto status =
      zx::clock::create(ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS, nullptr, &ref_clock);
  EXPECT_EQ(status, ZX_OK);

  // update starts the clock. Must use a rate_adjust that (when added to 1000000) isn't reduceable
  zx::clock::update_args args;
  args.reset().set_value(zx::time(0)).set_rate_adjust(+999);
  EXPECT_EQ(ref_clock.update(args), ZX_OK);

  zx_clock_details_v1_t clock_details;
  EXPECT_EQ(ref_clock.get_details(&clock_details), ZX_OK);
  DisplayClockDetails(clock_details);

  auto snapshot_result = SnapshotClock(ref_clock);
  EXPECT_TRUE(snapshot_result.is_ok());

  auto snapshot = snapshot_result.take_value();
  EXPECT_EQ(clock_details.generation_counter, snapshot.generation);
  auto mono_to_ref = snapshot.reference_to_monotonic.Inverse();
  EXPECT_EQ(clock_details.mono_to_synthetic.synthetic_offset, mono_to_ref.subject_time());
  EXPECT_EQ(clock_details.mono_to_synthetic.reference_offset, mono_to_ref.reference_time());
  EXPECT_EQ(clock_details.mono_to_synthetic.rate.synthetic_ticks, mono_to_ref.subject_delta());
  EXPECT_EQ(clock_details.mono_to_synthetic.rate.reference_ticks, mono_to_ref.reference_delta());
}

// Bracket a call to reference_clock.read, with two get_monotonic calls.
// The translated reference-clock time should be within the two monotonic values.
void PredictMonotonicTime(zx::clock& ref_clock) {
  zx_time_t now_ref;
  zx::time before_mono, after_mono, predicted_mono;
  zx_status_t status;

  before_mono = zx::clock::get_monotonic();
  status = ref_clock.read(&now_ref);
  after_mono = zx::clock::get_monotonic();
  EXPECT_EQ(status, ZX_OK);

  auto mono_time_result = MonotonicTimeFromReferenceTime(ref_clock, zx::time(now_ref));
  EXPECT_TRUE(mono_time_result.is_ok());

  predicted_mono = mono_time_result.take_value();
  EXPECT_GT(predicted_mono, before_mono) << "Predicted monotonic time too small.";
  EXPECT_LT(predicted_mono, after_mono) << "Predicted monotonic time too large.";
}

constexpr zx::duration kWaitDuration = zx::msec(35);
TEST(ClockUtilsTest, RefToMonoTime) {
  zx::clock ref_clock;

  zx_status_t status =
      zx::clock::create(ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS, nullptr, &ref_clock);
  EXPECT_EQ(status, ZX_OK);

  zx::clock::update_args args;
  args.reset().set_value(zx::time(0)).set_rate_adjust(-1000);
  status = ref_clock.update(args);
  EXPECT_EQ(status, ZX_OK);

  PredictMonotonicTime(ref_clock);

  for (auto repeats = 3u; repeats > 0; --repeats) {
    zx::nanosleep(zx::deadline_after(kWaitDuration));
    PredictMonotonicTime(ref_clock);
  }
}

// Bracket a call to get_monotonic, with two reference_clock.read calls.
// The translated monotonic time should be within the two reference_clock values.
void PredictReferenceTime(zx::clock& ref_clock) {
  zx_time_t before_ref, after_ref, predicted_ref;
  zx::time now_mono;
  zx_status_t status;

  ref_clock.read(&before_ref);
  now_mono = zx::clock::get_monotonic();
  status = ref_clock.read(&after_ref);
  EXPECT_EQ(status, ZX_OK);

  auto ref_time_result = ReferenceTimeFromMonotonicTime(ref_clock, now_mono);
  EXPECT_TRUE(ref_time_result.is_ok());

  predicted_ref = ref_time_result.take_value().get();
  EXPECT_GT(predicted_ref, before_ref) << "Predicted reference time too small.";
  EXPECT_LT(predicted_ref, after_ref) << "Predicted reference time too large.";
}

TEST(ClockUtilsTest, MonoToRefTime) {
  zx::clock ref_clock;

  zx_status_t status =
      zx::clock::create(ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS, nullptr, &ref_clock);
  EXPECT_EQ(status, ZX_OK);

  zx::clock::update_args args;
  args.reset().set_value(zx::time(987'654'321));
  status = ref_clock.update(args);
  EXPECT_EQ(status, ZX_OK);

  PredictReferenceTime(ref_clock);

  for (auto repeats = 3u; repeats > 0; --repeats) {
    zx::nanosleep(zx::deadline_after(kWaitDuration));
    PredictReferenceTime(ref_clock);
  }
}

// Make alternating readings from clock_A and clock_B: call them A1, B2, A3, B4.
// Translate B2 into clock_A's timeline as predict_A2, and A3 to clock_B's timeline as predict_B3.
// We expect strict sequencing of [time_A1,predict_A2,time_A3] and [time_B2,predict_B3,time_B4].
void PredictBetweenReferenceClocks(zx::clock& clock_A, zx::clock& clock_B) {
  zx::time time_A1, time_B2, time_A3, time_B4;

  clock_A.read(time_A1.get_address());
  clock_B.read(time_B2.get_address());
  clock_A.read(time_A3.get_address());
  clock_B.read(time_B4.get_address());

  auto predict_A2 = ReferenceTimeFromReferenceTime(clock_B, time_B2, clock_A).take_value();
  EXPECT_GT(predict_A2, time_A1) << "Translated reference time too small.";
  EXPECT_LT(predict_A2, time_A3) << "Translated reference time too large.";

  auto predict_B3 = ReferenceTimeFromReferenceTime(clock_A, time_A3, clock_B).take_value();
  EXPECT_GT(predict_B3, time_B2) << "Translated reference time too small.";
  EXPECT_LT(predict_B3, time_B4) << "Translated reference time too large.";
}

TEST(ClockUtilsTest, RefToRefTime) {
  zx::clock ref_clock_A, ref_clock_B;
  ASSERT_EQ(
      zx::clock::create(ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS | ZX_CLOCK_OPT_AUTO_START,
                        nullptr, &ref_clock_A),
      ZX_OK);
  ASSERT_EQ(
      zx::clock::create(ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS, nullptr, &ref_clock_B),
      ZX_OK);

  zx::clock::update_args args_A, args_B;
  args_A.reset().set_rate_adjust(-1000);
  args_B.reset().set_value(zx::time(987'654'321)).set_rate_adjust(1000);
  ASSERT_EQ(ref_clock_A.update(args_A), ZX_OK);
  ASSERT_EQ(ref_clock_B.update(args_B), ZX_OK);

  PredictBetweenReferenceClocks(ref_clock_A, ref_clock_B);

  for (auto repeats = 3u; repeats > 0; --repeats) {
    zx::nanosleep(zx::deadline_after(kWaitDuration));
    PredictBetweenReferenceClocks(ref_clock_A, ref_clock_B);
  }
}

TEST(ClockUtilsTest, TimelineToAffine) {
  auto tl_function = TimelineFunction(2, 3, 5, 7);
  auto affine_transform = ToAffineTransform(tl_function);

  EXPECT_EQ(affine_transform.a_offset(), tl_function.reference_time());
  EXPECT_EQ(affine_transform.b_offset(), tl_function.subject_time());
  EXPECT_EQ(affine_transform.numerator(), tl_function.subject_delta());
  EXPECT_EQ(affine_transform.denominator(), tl_function.reference_delta());
}

TEST(ClockUtilsTest, AffineToTimeline) {
  auto affine_transform = affine::Transform(11, 13, affine::Ratio(17, 19));
  auto tl_function = ToTimelineFunction(affine_transform);

  EXPECT_EQ(affine_transform.a_offset(), tl_function.reference_time());
  EXPECT_EQ(affine_transform.b_offset(), tl_function.subject_time());
  EXPECT_EQ(affine_transform.numerator(), tl_function.subject_delta());
  EXPECT_EQ(affine_transform.denominator(), tl_function.reference_delta());
}

}  // namespace

}  // namespace media::audio::clock
