// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_INPUT_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_INPUT_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/zx/channel.h>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/audio/audio_core/audio_device.h"
#include "src/media/audio/audio_core/reporter.h"

namespace media::audio {

class AudioDeviceManager;

class AudioInput : public AudioDevice {
 public:
  static std::shared_ptr<AudioInput> Create(const std::string& name, zx::channel channel,
                                            ThreadingModel* threading_model,
                                            DeviceRegistry* registry, LinkMatrix* link_matrix);

  static std::shared_ptr<AudioInput> Create(
      const std::string& name,
      fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig> stream_config,
      ThreadingModel* threading_model, DeviceRegistry* registry, LinkMatrix* link_matrix);

  AudioInput(const std::string& name, zx::channel channel, ThreadingModel* threading_model,
             DeviceRegistry* registry, LinkMatrix* link_matrix);
  AudioInput(const std::string& name,
             fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig> stream_config,
             ThreadingModel* threading_model, DeviceRegistry* registry, LinkMatrix* link_matrix);

  ~AudioInput() override = default;

 protected:
  // |media::audio::AudioObject|
  fit::result<std::shared_ptr<ReadableStream>, zx_status_t> InitializeDestLink(
      const AudioObject& dest) override;

  // |media::audio::AudioDevice|
  void ApplyGainLimits(fuchsia::media::AudioGainInfo* in_out_info,
                       fuchsia::media::AudioGainValidFlags set_flags) override;

  void SetGainInfo(const fuchsia::media::AudioGainInfo& info,
                   fuchsia::media::AudioGainValidFlags set_flags) override;

  zx_status_t Init() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void OnWakeup() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void OnDriverInfoFetched() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void OnDriverConfigComplete() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void OnDriverStartComplete() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void OnDriverStopComplete() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void OnDriverPlugStateChange(bool plugged, zx::time plug_time) override
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

 private:
  enum class State {
    Uninitialized,
    Initialized,
    FetchingFormats,
    Idle,
  };

  void UpdateDriverGainState() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  zx::channel initial_stream_channel_;
  State state_ = State::Uninitialized;
  Reporter::Container<Reporter::InputDevice>::Ptr reporter_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_INPUT_H_
