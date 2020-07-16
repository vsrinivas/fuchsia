// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_tuner_impl.h"

#include <lib/syslog/logger.h>

#include <filesystem>

#include "src/media/audio/audio_core/audio_device.h"
#include "src/media/audio/audio_core/audio_device_manager.h"
#include "src/media/audio/audio_core/stream_usage.h"
#include "src/media/audio/lib/effects_loader/effects_loader.h"

namespace media::audio {

fidl::InterfaceRequestHandler<fuchsia::media::tuning::AudioTuner>
AudioTunerImpl::GetFidlRequestHandler() {
  return bindings_.GetHandler(this);
}

void AudioTunerImpl::GetAvailableAudioEffects(GetAvailableAudioEffectsCallback callback) {
  std::vector<fuchsia::media::tuning::AudioEffectType> available_effects;
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

      fuchsia::media::tuning::AudioEffectType effect;
      effect.set_module_name(lib_name.string());
      effect.set_effect_name(desc.name);
      available_effects.push_back(std::move(effect));
    }
  }
  callback(std::move(available_effects));
}

void AudioTunerImpl::GetAudioDeviceProfile(std::string device_id,
                                           GetAudioDeviceProfileCallback callback) {
  auto unique_id = AudioDevice::UniqueIdFromString(device_id).take_value();
  DeviceConfig::OutputDeviceProfile tuning_profile =
      context_.process_config().device_config().output_device_profile(unique_id);
  VolumeCurve volume_curve = context_.process_config().default_volume_curve();
  callback(ToAudioDeviceTuningProfile(tuning_profile.pipeline_config(), volume_curve));
}

void AudioTunerImpl::GetDefaultAudioDeviceProfile(std::string device_id,
                                                  GetDefaultAudioDeviceProfileCallback callback) {
  auto unique_id = AudioDevice::UniqueIdFromString(device_id).take_value();
  DeviceConfig::OutputDeviceProfile default_profile =
      context_.process_config().device_config().output_device_profile(unique_id);
  VolumeCurve default_volume_curve = context_.process_config().default_volume_curve();
  callback(ToAudioDeviceTuningProfile(default_profile.pipeline_config(), default_volume_curve));
}

void AudioTunerImpl::SetAudioDeviceProfile(std::string device_id,
                                           fuchsia::media::tuning::AudioDeviceTuningProfile profile,
                                           SetAudioDeviceProfileCallback callback) {
  auto config = PipelineConfig(ToPipelineConfigMixGroup(std::move(profile.pipeline())));
  auto volume_curve = ToVolumeCurve(profile.volume_curve());
  auto promise = context_.device_manager().UpdatePipelineConfig(device_id, config, volume_curve);

  context_.threading_model().FidlDomain().executor()->schedule_task(
      promise.then([callback = std::move(callback)](fit::result<void, zx_status_t>& result) {
        if (result.is_ok()) {
          callback(ZX_OK);
        } else {
          callback(result.take_error());
        }
      }));
}

}  // namespace media::audio
