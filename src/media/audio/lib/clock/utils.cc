// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/utils.h"

#include <cmath>

#include "src/lib/syslog/cpp/logger.h"

namespace media::audio {

zx_status_t DuplicateClock(const zx::clock& original_clock, zx::clock* dupe_clock) {
  FX_DCHECK(dupe_clock) << "Null ptr passed to DuplicateClock";

  constexpr auto rights = ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ;
  auto status = original_clock.duplicate(rights, dupe_clock);

  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Could not duplicate the provided clock handle";
  }

  return status;
}

zx_status_t GetAndDisplayClockDetails(const zx::clock& ref_clock) {
  if (!ref_clock.is_valid()) {
    FX_LOGS(INFO) << "Clock is invalid";
    return ZX_OK;
  }

  zx_clock_details_v1_t clock_details;
  zx_status_t status = ref_clock.get_details(&clock_details);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Error calling zx::clock::get_details";
    return status;
  }

  DisplayClockDetails(clock_details);

  return ZX_OK;
}

void DisplayClockDetails(const zx_clock_details_v1_t& clock_details) {
  FX_LOGS(INFO) << "******************************************";
  FX_LOGS(INFO) << "Clock details -";
  FX_LOGS(INFO) << "  options:\t\t\t\t0x" << std::hex << clock_details.options;
  FX_LOGS(INFO) << "  backstop_time:\t\t\t" << clock_details.backstop_time;

  FX_LOGS(INFO) << "  query_ticks:\t\t\t" << clock_details.query_ticks;
  FX_LOGS(INFO) << "  last_value_update_ticks:\t\t" << clock_details.last_value_update_ticks;
  FX_LOGS(INFO) << "  last_rate_adjust_update_ticks:\t"
                << clock_details.last_rate_adjust_update_ticks;

  FX_LOGS(INFO) << "  generation_counter:\t\t" << clock_details.generation_counter;

  FX_LOGS(INFO) << "  mono_to_synthetic -";
  FX_LOGS(INFO) << "    reference_offset:\t\t" << clock_details.mono_to_synthetic.reference_offset;
  FX_LOGS(INFO) << "    synthetic_offset:\t\t" << clock_details.mono_to_synthetic.synthetic_offset;
  FX_LOGS(INFO) << "    rate -";
  FX_LOGS(INFO) << "      synthetic_ticks:\t\t"
                << clock_details.mono_to_synthetic.rate.synthetic_ticks;
  FX_LOGS(INFO) << "      reference_ticks:\t\t"
                << clock_details.mono_to_synthetic.rate.reference_ticks;
  FX_LOGS(INFO) << "******************************************";
}

fit::result<ClockSnapshot, zx_status_t> SnapshotClock(const zx::clock& ref_clock) {
  ClockSnapshot snapshot;
  zx_clock_details_v1_t clock_details;
  zx_status_t status = ref_clock.get_details(&clock_details);

  if (status != ZX_OK) {
    return fit::error(status);
  }

  snapshot.timeline_transform =
      TimelineFunction(clock_details.mono_to_synthetic.synthetic_offset,
                       clock_details.mono_to_synthetic.reference_offset,
                       clock_details.mono_to_synthetic.rate.synthetic_ticks,
                       clock_details.mono_to_synthetic.rate.reference_ticks);
  snapshot.generation = clock_details.generation_counter;

  return fit::ok(snapshot);
}

// Naming is confusing here. zx::clock transforms/structs call the underlying baseline clock (ticks
// or monotonic: we use monotonic) their "reference" clock. Unfortunately, in media terminology a
// "reference clock" could be any continuous monotonically increasing clock -- including not only
// the local system monotonic, but also custom clocks maintained outside the kernel (which zx::clock
// calls "synthetic" clocks).
//
// Thus in these util functions that convert between clocks, a conversion that we usually call "from
// monotonic to reference" is (in zx::clock terms) a conversion "from reference to synthetic", where
// the baseline reference here is the monotonic clock.
fit::result<zx::time, zx_status_t> ReferenceTimeFromMonotonicTime(const zx::clock& ref_clock,
                                                                  zx::time mono_time) {
  zx_clock_details_v1_t clock_details;
  zx_status_t status = ref_clock.get_details(&clock_details);
  if (status != ZX_OK) {
    return fit::error(status);
  }

  return fit::ok(zx::time(
      affine::Transform::Apply(clock_details.mono_to_synthetic.reference_offset,
                               clock_details.mono_to_synthetic.synthetic_offset,
                               affine::Ratio(clock_details.mono_to_synthetic.rate.synthetic_ticks,
                                             clock_details.mono_to_synthetic.rate.reference_ticks),
                               mono_time.get())));
}

fit::result<zx::time, zx_status_t> MonotonicTimeFromReferenceTime(const zx::clock& ref_clock,
                                                                  zx::time ref_time) {
  zx_clock_details_v1_t clock_details;
  zx_status_t status = ref_clock.get_details(&clock_details);
  if (status != ZX_OK) {
    return fit::error(status);
  }

  return fit::ok(zx::time(affine::Transform::ApplyInverse(
      clock_details.mono_to_synthetic.reference_offset,
      clock_details.mono_to_synthetic.synthetic_offset,
      affine::Ratio(clock_details.mono_to_synthetic.rate.synthetic_ticks,
                    clock_details.mono_to_synthetic.rate.reference_ticks),
      ref_time.get())));
}

affine::Transform ToAffineTransform(TimelineFunction& tl_function) {
  return affine::Transform(
      tl_function.reference_time(), tl_function.subject_time(),
      affine::Ratio(tl_function.subject_delta(), tl_function.reference_delta()));
}

TimelineFunction ToTimelineFunction(affine::Transform affine_trans) {
  return TimelineFunction(affine_trans.b_offset(), affine_trans.a_offset(),
                          affine_trans.numerator(), affine_trans.denominator());
}

}  // namespace media::audio
