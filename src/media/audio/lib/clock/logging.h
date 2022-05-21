// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_CLOCK_LOGGING_H_
#define SRC_MEDIA_AUDIO_LIB_CLOCK_LOGGING_H_

#include <lib/zx/time.h>

#include <optional>

#include "src/media/audio/lib/clock/clock.h"
#include "src/media/audio/lib/clock/pid_control.h"

namespace media_audio {

// Log an adjustment to a clock. This is typically called just before `clock.SetRate`.
void LogClockAdjustment(const Clock& clock, std::optional<int32_t> last_rate_ppm,
                        int32_t next_rate_ppm, zx::duration pos_error,
                        const ::media::audio::clock::PidControl& pid);

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_LIB_CLOCK_LOGGING_H_
