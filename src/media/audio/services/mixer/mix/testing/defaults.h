// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_TESTING_DEFAULTS_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_TESTING_DEFAULTS_H_

#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"

namespace media_audio {

// Can be used when any MixJobContext will do.
MixJobContext& DefaultCtx();

// A set of clock snapshots that can be used when any will do.
const ClockSnapshots& DefaultClockSnapshots();

// A reference clock to use when any clock will do.
// This clock is guaranteed to exist in `MixJobContext.clocks()` and `DefaultClockSnapshots()`.
zx_koid_t DefaultClockKoid();

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_TESTING_DEFAULTS_H_
