// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_INPUT_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_INPUT_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/zx/channel.h>

#include <fbl/ref_ptr.h>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/audio/audio_core/audio_device.h"
#include "src/media/audio/audio_core/fwd_decls.h"

namespace media::audio {

class AudioDeviceManager;

class AudioInput : public AudioDevice {
 public:
  static fbl::RefPtr<AudioInput> Create(zx::channel channel, ThreadingModel* threading_model,
                                        ObjectRegistry* registry);

 protected:
  zx_status_t Init() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void OnWakeup() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void OnDriverInfoFetched() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void OnDriverConfigComplete() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void OnDriverStartComplete() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void OnDriverStopComplete() override FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  void OnDriverPlugStateChange(bool plugged, zx_time_t plug_time) override
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

  friend class fbl::RefPtr<AudioInput>;

  AudioInput(zx::channel channel, ThreadingModel* threading_model, ObjectRegistry* registry);
  ~AudioInput() override{};

  void UpdateDriverGainState() FXL_EXCLUSIVE_LOCKS_REQUIRED(mix_domain().token());

  zx::channel initial_stream_channel_;
  State state_ = State::Uninitialized;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_INPUT_H_
