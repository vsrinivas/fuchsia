// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_CORE_AUDIO_INPUT_H_
#define GARNET_BIN_MEDIA_AUDIO_CORE_AUDIO_INPUT_H_

#include <lib/zx/channel.h>

#include "garnet/bin/media/audio_core/audio_device.h"
#include "garnet/bin/media/audio_core/audio_driver.h"

namespace media {
namespace audio {

class AudioDeviceManager;

class AudioInput : public AudioDevice {
 public:
  static fbl::RefPtr<AudioInput> Create(zx::channel channel,
                                        AudioDeviceManager* manager);

 protected:
  zx_status_t Init() override FXL_LOCKS_EXCLUDED(mix_domain_->token());

  void OnWakeup() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  void OnDriverInfoFetched() override
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  void OnDriverConfigComplete() override
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  void OnDriverStartComplete() override
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  void OnDriverStopComplete() override
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  void OnDriverPlugStateChange(bool plugged, zx_time_t plug_time) override
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  // AudioDevice impl
  void ApplyGainLimits(::fuchsia::media::AudioGainInfo* in_out_info,
                       uint32_t set_flags) override;

 private:
  enum class State {
    Uninitialized,
    Initialized,
    FetchingFormats,
    Idle,
  };

  friend class fbl::RefPtr<AudioInput>;

  AudioInput(zx::channel channel, AudioDeviceManager* manager);
  ~AudioInput() override{};

  void UpdateDriverGainState()
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain_->token());

  zx::channel initial_stream_channel_;
  State state_ = State::Uninitialized;
};

}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_CORE_AUDIO_INPUT_H_
