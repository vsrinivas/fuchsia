// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_TUNER_IMPL_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_TUNER_IMPL_H_

#include <fuchsia/media/tuning/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include <unordered_map>

#include "src/media/audio/audio_core/context.h"
#include "src/media/audio/audio_core/device_config.h"

namespace media::audio {

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
                             SetAudioDeviceProfileCallback callback) final{};
  void DeleteAudioDeviceProfile(std::string device_id,
                                DeleteAudioDeviceProfileCallback callback) final{};
  void SetAudioEffectConfig(std::string device_id, fuchsia::media::tuning::AudioEffectConfig effect,
                            SetAudioEffectConfigCallback callback) final{};

 private:
  const Context& context_;
  std::vector<fuchsia::media::tuning::AudioEffectType> available_effects_;
  fidl::BindingSet<fuchsia::media::tuning::AudioTuner, AudioTunerImpl*> bindings_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_TUNER_IMPL_H_
