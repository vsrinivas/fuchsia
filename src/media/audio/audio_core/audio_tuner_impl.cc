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
  auto tuned_specification_it = tuned_device_specifications_.find(device_id);
  auto device = tuned_specification_it != tuned_device_specifications_.end()
                    ? tuned_specification_it->second
                    : GetDefaultDeviceSpecification(device_id);
  callback(ToAudioDeviceTuningProfile(device.pipeline_config, device.volume_curve));
}

void AudioTunerImpl::GetDefaultAudioDeviceProfile(std::string device_id,
                                                  GetDefaultAudioDeviceProfileCallback callback) {
  auto default_device = GetDefaultDeviceSpecification(device_id);
  callback(ToAudioDeviceTuningProfile(default_device.pipeline_config, default_device.volume_curve));
}

void AudioTunerImpl::SetAudioDeviceProfile(std::string device_id,
                                           fuchsia::media::tuning::AudioDeviceTuningProfile profile,
                                           SetAudioDeviceProfileCallback callback) {
  auto config = PipelineConfig(ToPipelineConfigMixGroup(std::move(profile.pipeline())));
  auto volume_curve = ToVolumeCurve(profile.volume_curve());
  auto promise = context_.device_manager().UpdatePipelineConfig(device_id, config, volume_curve);

  context_.threading_model().FidlDomain().executor()->schedule_task(
      promise.then([this, device_id, config, volume_curve,
                    callback = std::move(callback)](fit::result<void, zx_status_t>& result) {
        if (result.is_ok()) {
          auto tuned_device_it = tuned_device_specifications_.find(device_id);
          if (tuned_device_it != tuned_device_specifications_.end()) {
            tuned_device_it->second =
                OutputDeviceSpecification{.pipeline_config = config, .volume_curve = volume_curve};
          } else {
            tuned_device_specifications_.emplace(
                device_id,
                OutputDeviceSpecification{.pipeline_config = config, .volume_curve = volume_curve});
          }
          callback(ZX_OK);
        } else {
          callback(result.take_error());
        }
      }));
}

void AudioTunerImpl::DeleteAudioDeviceProfile(std::string device_id,
                                              DeleteAudioDeviceProfileCallback callback) {
  if (tuned_device_specifications_.find(device_id) == tuned_device_specifications_.end()) {
    callback(ZX_OK);
    return;
  }

  auto default_device = GetDefaultDeviceSpecification(device_id);
  auto promise = context_.device_manager().UpdatePipelineConfig(
      device_id, default_device.pipeline_config, default_device.volume_curve);

  context_.threading_model().FidlDomain().executor()->schedule_task(promise.then(
      [this, device_id, callback = std::move(callback)](fit::result<void, zx_status_t>& result) {
        FX_CHECK(result.is_ok());
        tuned_device_specifications_.erase(device_id);
        callback(ZX_OK);
      }));
}

AudioTunerImpl::OutputDeviceSpecification AudioTunerImpl::GetDefaultDeviceSpecification(
    const std::string& device_id) {
  auto unique_id = AudioDevice::UniqueIdFromString(device_id).take_value();
  PipelineConfig device_profile =
      context_.process_config().device_config().output_device_profile(unique_id).pipeline_config();
  VolumeCurve volume_curve = context_.process_config().default_volume_curve();
  return OutputDeviceSpecification{.pipeline_config = device_profile, .volume_curve = volume_curve};
}

}  // namespace media::audio
