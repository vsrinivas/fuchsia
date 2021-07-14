// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/testing/clock_test.h"

#include <lib/affine/transform.h>

#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/clone_mono.h"

namespace media::audio::clock::testing {

fpromise::result<zx::clock, zx_status_t> CreateCustomClock(ClockProperties props) {
  zx::clock clock;
  zx_status_t status;

  if (props.start_val.has_value()) {
    status = zx::clock::create(ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS, nullptr, &clock);
    if (status != ZX_OK) {
      return fpromise::error(status);
    }

    zx::clock::update_args args;
    args.reset().set_value(props.start_val.value());

    status = clock.update(args);
    if (status != ZX_OK) {
      return fpromise::error(status);
    }
  } else {
    clock = props.rate_adjust_ppm.has_value() ? clock::AdjustableCloneOfMonotonic()
                                              : clock::CloneOfMonotonic();
  }

  if (props.rate_adjust_ppm.has_value()) {
    zx::clock::update_args args;
    args.reset().set_rate_adjust(props.rate_adjust_ppm.value());

    status = clock.update(args);
    if (status != ZX_OK) {
      return fpromise::error(status);
    }

    return fpromise::ok(std::move(clock));
  }

  status = clock.replace(ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ, &clock);
  if (status != ZX_OK) {
    return fpromise::error(status);
  }

  return fpromise::ok(std::move(clock));
}

fpromise::result<zx::duration, zx_status_t> GetOffsetFromMonotonic(const zx::clock& clock) {
  if (!clock.is_valid()) {
    return fpromise::error(ZX_ERR_INVALID_ARGS);
  }

  zx_clock_details_v1_t clock_details;
  zx_status_t status = clock.get_details(&clock_details);
  if (status != ZX_OK) {
    return fpromise::error(status);
  }

  auto synthetic_per_mono = affine::Ratio(clock_details.mono_to_synthetic.rate.synthetic_ticks,
                                          clock_details.mono_to_synthetic.rate.reference_ticks);
  auto synthetic_offset_from_mono = affine::Transform::Apply(
      clock_details.mono_to_synthetic.reference_offset,
      clock_details.mono_to_synthetic.synthetic_offset, synthetic_per_mono, 0);

  return fpromise::ok(zx::duration(synthetic_offset_from_mono));
}

// Ensure this reference clock's handle has expected rights: DUPLICATE, TRANSFER, READ, not WRITE.
void VerifyReadOnlyRights(const zx::clock& ref_clock) {
  constexpr auto rights = ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ;
  zx::clock dupe_clock;

  EXPECT_EQ(ref_clock.duplicate(rights, &dupe_clock), ZX_OK);
  EXPECT_NE(ref_clock.duplicate(rights | ZX_RIGHT_WRITE, &dupe_clock), ZX_OK);
}

constexpr zx_duration_t kWaitInterval = ZX_USEC(50);
void VerifyAdvances(const zx::clock& clock) {
  zx_time_t before, after;
  EXPECT_EQ(clock.read(&before), ZX_OK) << "clock.read failed";

  zx_nanosleep(zx_deadline_after(kWaitInterval));
  EXPECT_GE(clock.read(&after), ZX_OK) << "clock.read failed";
  EXPECT_GE(after - before, kWaitInterval);
}

// ...try to rate-adjust this clock -- this should fail
void VerifyCannotBeRateAdjusted(const zx::clock& clock) {
  zx::clock::update_args args;
  args.reset().set_rate_adjust(+12);

  EXPECT_NE(clock.update(args), ZX_OK) << "clock.update with rate_adjust should fail";
}

// Rate-adjusting this clock should succeed. Validate that the rate change took effect and
// rate_adjust_update_ticks is later than a tick reading before calling set_rate_adjust.
void VerifyCanBeRateAdjusted(const zx::clock& clock) {
  zx_time_t ref_before;
  EXPECT_EQ(clock.read(&ref_before), ZX_OK) << "clock.read failed";

  zx_clock_details_v1_t clock_details;
  EXPECT_EQ(clock.get_details(&clock_details), ZX_OK);

  auto ticks_before = affine::Transform::ApplyInverse(
      clock_details.ticks_to_synthetic.reference_offset,
      clock_details.ticks_to_synthetic.synthetic_offset,
      affine::Ratio(clock_details.ticks_to_synthetic.rate.synthetic_ticks,
                    clock_details.ticks_to_synthetic.rate.reference_ticks),
      ref_before);

  zx_nanosleep(zx_deadline_after(kWaitInterval));

  zx::clock::update_args args;
  args.reset().set_rate_adjust(-100);
  EXPECT_EQ(clock.update(args), ZX_OK) << "clock.update with rate_adjust failed";

  EXPECT_EQ(clock.get_details(&clock_details), ZX_OK);

  EXPECT_GT(clock_details.last_rate_adjust_update_ticks, ticks_before);
  EXPECT_EQ(clock_details.mono_to_synthetic.rate.synthetic_ticks, 999'900u);
}

// Validate that given clock is identical to CLOCK_MONOTONIC
void VerifyIsSystemMonotonic(const zx::clock& clock) {
  zx_clock_details_v1_t clock_details;
  EXPECT_EQ(clock.get_details(&clock_details), ZX_OK);

  EXPECT_EQ(clock_details.mono_to_synthetic.reference_offset,
            clock_details.mono_to_synthetic.synthetic_offset);
  EXPECT_EQ(clock_details.mono_to_synthetic.rate.reference_ticks,
            clock_details.mono_to_synthetic.rate.synthetic_ticks);
}

// Validate that given clock is NOT identical to CLOCK_MONOTONIC
void VerifyIsNotSystemMonotonic(const zx::clock& clock) {
  zx_clock_details_v1_t clock_details;
  EXPECT_EQ(clock.get_details(&clock_details), ZX_OK);

  EXPECT_FALSE(clock_details.mono_to_synthetic.reference_offset ==
                   clock_details.mono_to_synthetic.synthetic_offset &&
               clock_details.mono_to_synthetic.rate.reference_ticks ==
                   clock_details.mono_to_synthetic.rate.synthetic_ticks);
}

}  // namespace media::audio::clock::testing
