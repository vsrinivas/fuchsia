// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_PROFILE_ACQUIRER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_PROFILE_ACQUIRER_H_

#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/profile.h>
#include <stdint.h>
#include <zircon/types.h>

#include "src/media/audio/audio_core/shared/mix_profile_config.h"

namespace media::audio {

zx_status_t AcquireHighPriorityProfile(const MixProfileConfig& mix_profile_config,
                                       zx::profile* profile);

void AcquireAudioCoreImplProfile(sys::ComponentContext* context,
                                 fit::function<void(zx_status_t, zx::profile)> callback);

void AcquireRelativePriorityProfile(uint32_t priority, sys::ComponentContext* context,
                                    fit::function<void(zx_status_t, zx::profile)> callback);

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_SHARED_PROFILE_ACQUIRER_H_
