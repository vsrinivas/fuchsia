// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_CLOCK_TESTING_CLOCK_TEST_H_
#define SRC_MEDIA_AUDIO_LIB_CLOCK_TESTING_CLOCK_TEST_H_

#include <lib/zx/clock.h>

namespace media::audio::testing {

// Clock should have rights DUPLICATE, TRANSFER, READ; clock should not have WRITE
void VerifyAppropriateRights(const zx::clock& ref_clock);
void VerifyClockAdvances(const zx::clock& ref_clock);

void VerifyClockCannotBeRateAdjusted(const zx::clock& ref_clock);
void VerifyClockCanBeRateAdjusted(const zx::clock& ref_clock);

zx::clock CreateClockForSamenessTest();
void VerifySameClock(const zx::clock& clock1, const zx::clock& clock2);

void VerifyClockIsSystemMonotonic(const zx::clock& clock);
void VerifyClockIsNotSystemMonotonic(const zx::clock& clock);

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_LIB_CLOCK_TESTING_CLOCK_TEST_H_
