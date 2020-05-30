// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_tuner_impl.h"

namespace media::audio {

fidl::InterfaceRequestHandler<fuchsia::media::tuning::AudioTuner>
AudioTunerImpl::GetFidlRequestHandler() {
  return bindings_.GetHandler(this);
}

void AudioTunerImpl::GetAvailableAudioEffects(GetAvailableAudioEffectsCallback callback) {
  // Return empty available_effects_ until implemented
  callback(available_effects_);
}

}  // namespace media::audio
