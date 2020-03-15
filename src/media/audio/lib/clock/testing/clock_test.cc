// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/testing/clock_test.h"

#include <lib/affine/transform.h>

#include <gtest/gtest.h>

namespace media::audio::testing {

// Ensure that this clock's handle has the expected rights.
void VerifyAppropriateRights(const zx::clock& ref_clock) {
  constexpr auto rights = ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ;
  zx::clock dupe_clock;

  EXPECT_EQ(ref_clock.duplicate(rights, &dupe_clock), ZX_OK);
  EXPECT_NE(ref_clock.duplicate(rights | ZX_RIGHT_WRITE, &dupe_clock), ZX_OK);
}

constexpr zx_duration_t kWaitInterval = ZX_USEC(50);
void VerifyClockAdvances(const zx::clock& ref_clock) {
  zx_time_t before, after;
  EXPECT_EQ(ref_clock.read(&before), ZX_OK) << "ref_clock.read failed";

  zx_nanosleep(zx_deadline_after(kWaitInterval));
  EXPECT_GE(ref_clock.read(&after), ZX_OK) << "ref_clock.read failed";
  EXPECT_GE(after - before, kWaitInterval);
}

// ...try to rate-adjust this clock -- this should fail
void VerifyClockCannotBeRateAdjusted(const zx::clock& ref_clock) {
  zx::clock::update_args args;
  args.reset().set_rate_adjust(+12);

  EXPECT_NE(ref_clock.update(args), ZX_OK);
}

// Rate-adjusting this clock should succeed. Validate that the rate change took effect and
// rate_adjust_update_ticks is later than a tick reading before calling set_rate_adjust.
void VerifyClockCanBeRateAdjusted(const zx::clock& ref_clock) {
  zx_time_t ref_before;
  EXPECT_EQ(ref_clock.read(&ref_before), ZX_OK) << "ref_clock.read failed";

  zx_clock_details_v1_t clock_details;
  EXPECT_EQ(ref_clock.get_details(&clock_details), ZX_OK);

  auto ticks_before = affine::Transform::ApplyInverse(
      clock_details.ticks_to_synthetic.reference_offset,
      clock_details.ticks_to_synthetic.synthetic_offset,
      affine::Ratio(clock_details.ticks_to_synthetic.rate.synthetic_ticks,
                    clock_details.ticks_to_synthetic.rate.reference_ticks),
      ref_before);

  zx_nanosleep(zx_deadline_after(kWaitInterval));

  zx::clock::update_args args;
  args.reset().set_rate_adjust(-100);
  EXPECT_EQ(ref_clock.update(args), ZX_OK);

  EXPECT_EQ(ref_clock.get_details(&clock_details), ZX_OK);

  EXPECT_GT(clock_details.last_rate_adjust_update_ticks, ticks_before);
  EXPECT_EQ(clock_details.mono_to_synthetic.rate.synthetic_ticks, 999'900u);
}

// Create a "marked" clock, to verify that another object points to the same clock
// This clock is also guaranteed to differ from CLOCK_MONOTONIC.
constexpr uint64_t kErrorBoundMarker = 0x1234;
zx::clock CreateClockForSamenessTest() {
  zx::clock marked_clock;
  EXPECT_EQ(
      zx::clock::create(ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS, nullptr, &marked_clock),
      ZX_OK);

  // Use this to validate that we receive the same custom clock that we set
  zx::clock::update_args args;
  args.reset().set_value(zx::time(0)).set_error_bound(kErrorBoundMarker);
  EXPECT_EQ(marked_clock.update(args), ZX_OK);

  return marked_clock;
}

// Validate that clock2 points to the same underlying clock1
void VerifySameClock(const zx::clock& clock1, const zx::clock& clock2) {
  zx_clock_details_v1_t clock1_details, clock2_details;
  EXPECT_EQ(clock1.get_details(&clock1_details), ZX_OK);
  EXPECT_EQ(clock2.get_details(&clock2_details), ZX_OK);

  EXPECT_EQ(clock1_details.options, clock2_details.options);
  EXPECT_EQ(clock1_details.error_bound, kErrorBoundMarker);
  EXPECT_EQ(clock2_details.error_bound, kErrorBoundMarker);
  EXPECT_EQ(clock1_details.last_error_bounds_update_ticks,
            clock2_details.last_error_bounds_update_ticks);
}

// Validate that given clock is identical to CLOCK_MONOTONIC
void VerifyClockIsSystemMonotonic(const zx::clock& clock) {
  zx_clock_details_v1_t clock_details;
  EXPECT_EQ(clock.get_details(&clock_details), ZX_OK);

  EXPECT_EQ(clock_details.mono_to_synthetic.reference_offset,
            clock_details.mono_to_synthetic.synthetic_offset);
  EXPECT_EQ(clock_details.mono_to_synthetic.rate.reference_ticks,
            clock_details.mono_to_synthetic.rate.synthetic_ticks);
}

// Validate that given clock is NOT identical to CLOCK_MONOTONIC
void VerifyClockIsNotSystemMonotonic(const zx::clock& clock) {
  zx_clock_details_v1_t clock_details;
  EXPECT_EQ(clock.get_details(&clock_details), ZX_OK);

  EXPECT_FALSE(clock_details.mono_to_synthetic.reference_offset ==
                   clock_details.mono_to_synthetic.synthetic_offset &&
               clock_details.mono_to_synthetic.rate.reference_ticks ==
                   clock_details.mono_to_synthetic.rate.synthetic_ticks);
}

}  // namespace media::audio::testing
