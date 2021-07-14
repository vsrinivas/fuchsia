// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_AUDIO_CLOCK_HELPER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_AUDIO_CLOCK_HELPER_H_

#include <lib/zx/clock.h>

#include <memory>

#include "src/media/audio/audio_core/audio_clock_factory.h"

namespace media::audio {
class AudioClock;
}

namespace media::audio::audio_clock_helper {

void VerifyReadOnlyRights(const AudioClock& audio_clock);

void VerifyAdvances(const AudioClock& audio_clock,
                    std::shared_ptr<AudioClockFactory> clock_factory);

void VerifyCannotBeRateAdjusted(const AudioClock& audio_clock);
void VerifyCanBeRateAdjusted(const AudioClock& audio_clock);

void VerifySame(const AudioClock& audio_clock1, const AudioClock& audio_clock2);
void VerifyNotSame(const AudioClock& audio_clock1, const AudioClock& audio_clock2);

void VerifyIsSystemMonotonic(const AudioClock& audio_clock);
void VerifyIsNotSystemMonotonic(const AudioClock& audio_clock);

}  // namespace media::audio::audio_clock_helper

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_AUDIO_CLOCK_HELPER_H_
