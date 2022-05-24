// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_MIXER_SERVICE_MIX_TESTING_DEFAULTS_H_
#define SRC_MEDIA_AUDIO_MIXER_SERVICE_MIX_TESTING_DEFAULTS_H_

#include "src/media/audio/mixer_service/mix/mix_job_context.h"

namespace media_audio_mixer_service {

// Can be used when any MixJobContext will do.
inline MixJobContext kDefaultCtx;

}  // namespace media_audio_mixer_service

#endif  // SRC_MEDIA_AUDIO_MIXER_SERVICE_MIX_TESTING_DEFAULTS_H_
