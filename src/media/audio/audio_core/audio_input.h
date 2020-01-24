// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_INPUT_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_INPUT_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/zx/channel.h>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/audio/audio_core/audio_device.h"

namespace media::audio {

class AudioDeviceManager;

class AudioInput : public AudioDevice {
 public:
  static std::shared_ptr<AudioInput> Create(zx::channel channel, ThreadingModel* threading_model,
                                            DeviceRegistry* registry, LinkMatrix* link_matrix);

  AudioInput(zx::channel channel, ThreadingModel* threading_model, DeviceRegistry* registry,
             LinkMatrix* link_matrix);

  ~AudioInput() override = default;

 protected:
  zx_status_t Init() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void OnWakeup() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void OnDriverInfoFetched() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void OnDriverConfigComplete() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void OnDriverStartComplete() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void OnDriverStopComplete() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void OnDriverPlugStateChange(bool plugged, zx::time plug_time) override
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  // AudioDevice impl
  void ApplyGainLimits(fuchsia::media::AudioGainInfo* in_out_info, uint32_t set_flags) override;

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
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_INPUT_H_
