// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_tuner_impl.h"

#include <lib/syslog/logger.h>

#include <filesystem>

#include "src/media/audio/audio_core/audio_device.h"
#include "src/media/audio/audio_core/stream_usage.h"
#include "src/media/audio/lib/effects_loader/effects_loader.h"

namespace media::audio {
namespace {

std::optional<fuchsia::media::tuning::StreamType> StreamTypeFromRenderUsage(RenderUsage usage) {
  switch (usage) {
    case RenderUsage::BACKGROUND:
      return fuchsia::media::tuning::StreamType::RENDER_BACKGROUND;
    case RenderUsage::MEDIA:
      return fuchsia::media::tuning::StreamType::RENDER_MEDIA;
    case RenderUsage::INTERRUPTION:
      return fuchsia::media::tuning::StreamType::RENDER_INTERRUPTION;
    case RenderUsage::SYSTEM_AGENT:
      return fuchsia::media::tuning::StreamType::RENDER_SYSTEM_AGENT;
    case RenderUsage::COMMUNICATION:
      return fuchsia::media::tuning::StreamType::RENDER_COMMUNICATION;
    case RenderUsage::ULTRASOUND:
      return fuchsia::media::tuning::StreamType::RENDER_ULTRASOUND;
    default:
      return std::nullopt;
  }
}

fuchsia::media::tuning::AudioEffectConfig ToAudioEffectConfig(const PipelineConfig::Effect effect) {
  auto result = fuchsia::media::tuning::AudioEffectConfig();
  result.set_instance_name(effect.instance_name);
  fuchsia::media::tuning::AudioEffectType effect_type;
  effect_type.set_module_name(effect.lib_name);
  effect_type.set_effect_name(effect.effect_name);
  result.set_type(std::move(effect_type));
  result.set_configuration(effect.effect_config);
  if (effect.output_channels.has_value()) {
    result.set_output_channels(effect.output_channels.value());
  }
  return result;
}

fuchsia::media::tuning::AudioMixGroup ToAudioMixGroup(const PipelineConfig::MixGroup mix_group) {
  std::string name = mix_group.name;
  bool loopback = mix_group.loopback;
  std::vector<fuchsia::media::tuning::AudioEffectConfig> effects;
  for (auto effect : mix_group.effects) {
    effects.push_back(ToAudioEffectConfig(effect));
  }

  std::vector<std::unique_ptr<fuchsia::media::tuning::AudioMixGroup>> inputs;
  for (auto input : mix_group.inputs) {
    inputs.push_back(
        std::make_unique<fuchsia::media::tuning::AudioMixGroup>(ToAudioMixGroup(input)));
  }
  std::vector<fuchsia::media::tuning::StreamType> streams;
  for (auto usage : mix_group.input_streams) {
    auto stream = StreamTypeFromRenderUsage(usage);
    if (stream.has_value()) {
      streams.push_back(stream.value());
    }
  }
  uint32_t output_rate = mix_group.output_rate;
  uint16_t output_channels = mix_group.output_channels;
  return fuchsia::media::tuning::AudioMixGroup{
      name, loopback, std::move(effects), std::move(inputs), streams, output_rate, output_channels};
}

fuchsia::media::tuning::AudioDeviceTuningProfile ToAudioDeviceTuningProfile(
    const DeviceConfig::OutputDeviceProfile profile, const VolumeCurve curve) {
  PipelineConfig pipeline_config = profile.pipeline_config();
  PipelineConfig::MixGroup root = pipeline_config.root();
  fuchsia::media::tuning::AudioMixGroup pipeline = ToAudioMixGroup(root);

  std::vector<fuchsia::media::tuning::Volume> volume_curve;
  for (auto mapping : curve.mappings()) {
    volume_curve.push_back(fuchsia::media::tuning::Volume{mapping.volume, mapping.gain_dbfs});
  }
  fuchsia::media::tuning::AudioDeviceTuningProfile final_profile;
  *final_profile.mutable_pipeline() = std::move(pipeline);
  final_profile.set_volume_curve(volume_curve);
  return final_profile;
}

}  // namespace

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
  callback(ToAudioDeviceTuningProfile(tuning_profile, volume_curve));
}

void AudioTunerImpl::GetDefaultAudioDeviceProfile(std::string device_id,
                                                  GetDefaultAudioDeviceProfileCallback callback) {
  auto unique_id = AudioDevice::UniqueIdFromString(device_id).take_value();
  DeviceConfig::OutputDeviceProfile default_profile =
      context_.process_config().device_config().output_device_profile(unique_id);
  VolumeCurve default_volume_curve = context_.process_config().default_volume_curve();
  callback(ToAudioDeviceTuningProfile(default_profile, default_volume_curve));
}

}  // namespace media::audio
