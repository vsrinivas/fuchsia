// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_CLOCK_TESTING_CLOCK_TEST_H_
#define SRC_MEDIA_AUDIO_LIB_CLOCK_TESTING_CLOCK_TEST_H_

#include <lib/fpromise/result.h>
#include <lib/zx/clock.h>

#include <optional>

namespace media::audio::clock::testing {

// Clock should have rights DUPLICATE, TRANSFER, READ; clock should not have WRITE
void VerifyReadOnlyRights(const zx::clock& ref_clock);
void VerifyAdvances(const zx::clock& ref_clock);

void VerifyCannotBeRateAdjusted(const zx::clock& ref_clock);
void VerifyCanBeRateAdjusted(const zx::clock& ref_clock);

void VerifyIsSystemMonotonic(const zx::clock& clock);
void VerifyIsNotSystemMonotonic(const zx::clock& clock);

struct ClockProperties {
  std::optional<zx::time> start_val = std::nullopt;
  std::optional<int32_t> rate_adjust_ppm = std::nullopt;
};

fpromise::result<zx::clock, zx_status_t> CreateCustomClock(ClockProperties props);
fpromise::result<zx::duration, zx_status_t> GetOffsetFromMonotonic(const zx::clock& clock);

}  // namespace media::audio::clock::testing

#endif  // SRC_MEDIA_AUDIO_LIB_CLOCK_TESTING_CLOCK_TEST_H_
