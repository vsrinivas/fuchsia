// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_tuner_impl.h"

#include <lib/syslog/logger.h>

#include <filesystem>

#include "src/media/audio/lib/effects_loader/effects_loader.h"

namespace media::audio {

fidl::InterfaceRequestHandler<fuchsia::media::tuning::AudioTuner>
AudioTunerImpl::GetFidlRequestHandler() {
  return bindings_.GetHandler(this);
}

void AudioTunerImpl::GetAvailableAudioEffects(GetAvailableAudioEffectsCallback callback) {
  for (auto& file : std::filesystem::directory_iterator("/pkg/lib")) {
    if (file.is_directory()) {
      continue;
    }

    auto lib_name = file.path().filename();
    std::unique_ptr<EffectsLoader> loader;
    zx_status_t status = EffectsLoader::CreateWithModule(lib_name.c_str(), &loader);
    if (status != ZX_OK) {
      continue;
    }

    for (uint32_t id = 0; id < loader->GetNumEffects(); ++id) {
      fuchsia_audio_effects_description desc;
      zx_status_t status = loader->GetEffectInfo(id, &desc);
      if (status != ZX_OK) {
        continue;
      }

      fuchsia::media::tuning::AudioEffectType effect = {.module_name = lib_name.string(),
                                                        .effect_name = desc.name};
      available_effects_.push_back(effect);
    }
  }
  callback(available_effects_);
}

}  // namespace media::audio
