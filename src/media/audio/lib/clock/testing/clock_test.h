// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_CLOCK_TESTING_CLOCK_TEST_H_
#define SRC_MEDIA_AUDIO_LIB_CLOCK_TESTING_CLOCK_TEST_H_

#include <lib/fit/result.h>
#include <lib/zx/clock.h>

#include <optional>

namespace media::audio::clock::testing {

// Clock should have rights DUPLICATE, TRANSFER, READ; clock should not have WRITE
void VerifyReadOnlyRights(const zx::clock& ref_clock);
void VerifyAdvances(const zx::clock& ref_clock);

void VerifyCannotBeRateAdjusted(const zx::clock& ref_clock);
void VerifyCanBeRateAdjusted(const zx::clock& ref_clock);

// We want to distinguish whether two handles point to the SAME underlying clock object, as opposed
// to simply having the same start times and rates (the latter would be the case for two clones of
// CLOCK_MONOTONIC). To do this, in CreateClockForSamenessTest we set a specific value in the
// clock's "error_bound" field. This field is not changed by AudioCore or other parties, so it
// serves as a unique marker for this underlying object.
zx::clock CreateForSamenessTest();
void VerifySame(const zx::clock& clock1, const zx::clock& clock2);
void VerifyNotSame(const zx::clock& clock1, const zx::clock& clock2);

void VerifyIsSystemMonotonic(const zx::clock& clock);
void VerifyIsNotSystemMonotonic(const zx::clock& clock);

struct ClockProperties {
  std::optional<zx::time> start_val = std::nullopt;
  std::optional<zx::duration> mono_offset = std::nullopt;
  std::optional<int32_t> rate_adjust_ppm = std::nullopt;
};

fit::result<zx::clock, zx_status_t> CreateCustomClock(ClockProperties props);

}  // namespace media::audio::clock::testing

#endif  // SRC_MEDIA_AUDIO_LIB_CLOCK_TESTING_CLOCK_TEST_H_
