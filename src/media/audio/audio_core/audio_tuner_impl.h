// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_TUNER_IMPL_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_TUNER_IMPL_H_

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/media/tuning/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include <unordered_map>

#include "src/media/audio/audio_core/context.h"
#include "src/media/audio/audio_core/device_config.h"

namespace media::audio {

using fuchsia::media::tuning::StreamType;

inline std::optional<StreamType> StreamTypeFromRenderUsage(RenderUsage usage) {
  switch (usage) {
    case RenderUsage::BACKGROUND:
      return StreamType::RENDER_BACKGROUND;
    case RenderUsage::MEDIA:
      return StreamType::RENDER_MEDIA;
    case RenderUsage::INTERRUPTION:
      return StreamType::RENDER_INTERRUPTION;
    case RenderUsage::SYSTEM_AGENT:
      return StreamType::RENDER_SYSTEM_AGENT;
    case RenderUsage::COMMUNICATION:
      return StreamType::RENDER_COMMUNICATION;
    case RenderUsage::ULTRASOUND:
      return StreamType::RENDER_ULTRASOUND;
    default:
      return std::nullopt;
  }
}

inline std::optional<RenderUsage> RenderUsageFromStreamType(StreamType usage) {
  switch (usage) {
    case StreamType::RENDER_BACKGROUND:
      return RenderUsage::BACKGROUND;
    case StreamType::RENDER_MEDIA:
      return RenderUsage::MEDIA;
    case StreamType::RENDER_INTERRUPTION:
      return RenderUsage::INTERRUPTION;
    case StreamType::RENDER_SYSTEM_AGENT:
      return RenderUsage::SYSTEM_AGENT;
    case StreamType::RENDER_COMMUNICATION:
      return RenderUsage::COMMUNICATION;
    case StreamType::RENDER_ULTRASOUND:
      return RenderUsage::ULTRASOUND;
    default:
      return std::nullopt;
  }
}

inline fuchsia::media::tuning::AudioEffectConfig ToAudioEffectConfig(
    const PipelineConfig::Effect effect) {
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

inline fuchsia::media::tuning::AudioMixGroup ToAudioMixGroup(
    const PipelineConfig::MixGroup mix_group) {
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
  std::vector<StreamType> streams;
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

inline fuchsia::media::tuning::AudioDeviceTuningProfile ToAudioDeviceTuningProfile(
    const PipelineConfig pipeline_config, const VolumeCurve curve) {
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

inline PipelineConfig::MixGroup ToPipelineConfigMixGroup(
    const fuchsia::media::tuning::AudioMixGroup& mix_group) {
  std::string name = mix_group.name;
  std::vector<RenderUsage> input_streams;
  for (auto s : mix_group.streams) {
    auto stream = RenderUsageFromStreamType(s);
    if (stream.has_value()) {
      input_streams.push_back(stream.value());
    }
  }
  std::vector<PipelineConfig::Effect> effects;
  for (size_t i = 0; i < mix_group.effects.size(); ++i) {
    effects.push_back(PipelineConfig::Effect{
        mix_group.effects[i].type().module_name(), mix_group.effects[i].type().effect_name(),
        mix_group.effects[i].instance_name(), mix_group.effects[i].configuration(), std::nullopt});
    if (mix_group.effects[i].has_output_channels()) {
      effects[i].output_channels = mix_group.effects[i].output_channels();
    }
  }
  std::vector<PipelineConfig::MixGroup> inputs;
  for (size_t i = 0; i < mix_group.inputs.size(); ++i) {
    inputs.push_back(ToPipelineConfigMixGroup(std::move(*mix_group.inputs[i])));
  }
  bool loopback = mix_group.loopback;
  uint32_t output_rate =
      mix_group.output_rate ? mix_group.output_rate : PipelineConfig::kDefaultMixGroupRate;
  uint16_t output_channels = mix_group.output_channels ? mix_group.output_channels
                                                       : PipelineConfig::kDefaultMixGroupChannels;

  return PipelineConfig::MixGroup{name,     input_streams, effects,        inputs,
                                  loopback, output_rate,   output_channels};
}

inline VolumeCurve ToVolumeCurve(const std::vector<fuchsia::media::tuning::Volume> volume_curve) {
  std::vector<VolumeCurve::VolumeMapping> mappings;
  for (auto vol : volume_curve) {
    mappings.push_back(VolumeCurve::VolumeMapping(vol.level, vol.decibel));
  }
  return VolumeCurve::FromMappings(std::move(mappings)).take_value();
}

class AudioTunerImpl : public fuchsia::media::tuning::AudioTuner {
 public:
  explicit AudioTunerImpl(Context& context) : context_(context) {}

  fidl::InterfaceRequestHandler<fuchsia::media::tuning::AudioTuner> GetFidlRequestHandler();

  // |fuchsia::media::tuning::AudioTuner|
  void GetAvailableAudioEffects(GetAvailableAudioEffectsCallback callback) final;
  void GetAudioDeviceProfile(std::string device_id, GetAudioDeviceProfileCallback callback) final;
  void GetDefaultAudioDeviceProfile(std::string device_id,
                                    GetDefaultAudioDeviceProfileCallback callback) final;
  void SetAudioDeviceProfile(std::string device_id,
                             fuchsia::media::tuning::AudioDeviceTuningProfile profile,
                             SetAudioDeviceProfileCallback callback) final;
  void DeleteAudioDeviceProfile(std::string device_id,
                                DeleteAudioDeviceProfileCallback callback) final;
  void SetAudioEffectConfig(std::string device_id, fuchsia::media::tuning::AudioEffectConfig effect,
                            SetAudioEffectConfigCallback callback) final;

 private:
  struct OutputDeviceSpecification {
    PipelineConfig pipeline_config;
    VolumeCurve volume_curve;
  };
  OutputDeviceSpecification GetDefaultDeviceSpecification(const std::string& device_id);
  bool UpdateTunedDeviceSpecification(const std::string& device_id,
                                      const fuchsia::media::tuning::AudioEffectConfig& effect);
  bool UpdateTunedEffectConfig(PipelineConfig::MixGroup& root, const std::string& instance_name,
                               const std::string& config);

  Context& context_;
  fidl::BindingSet<fuchsia::media::tuning::AudioTuner, AudioTunerImpl*> bindings_;
  std::unordered_map<std::string, OutputDeviceSpecification> tuned_device_specifications_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_TUNER_IMPL_H_
