// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_AUDIO_CORE_CLIENT_H_
#define SRC_MEDIA_AUDIO_AUDIO_AUDIO_CORE_CLIENT_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>

#include "lib/fidl/cpp/binding_set.h"
#include "src/lib/fxl/logging.h"

namespace media::audio {
class AudioCoreClient : public fuchsia::media::Audio {
 public:
  AudioCoreClient(sys::ComponentContext* startup_context,
                  fit::closure quit_callback);

  // Audio implementation.
  void CreateAudioRenderer(fidl::InterfaceRequest<fuchsia::media::AudioRenderer>
                               audio_renderer_request) final {
    audio_core_->CreateAudioRenderer(std::move(audio_renderer_request));
  };

  void CreateAudioCapturer(fidl::InterfaceRequest<fuchsia::media::AudioCapturer>
                               audio_capturer_request,
                           bool loopback) final {
    audio_core_->CreateAudioCapturer(loopback,
                                     std::move(audio_capturer_request));
  };

  void SetSystemGain(float gain_db) final {
    audio_core_->SetSystemGain(gain_db);
  };

  void SetSystemMute(bool muted) final { audio_core_->SetSystemMute(muted); };

  void SetRoutingPolicy(fuchsia::media::AudioOutputRoutingPolicy policy) final {
    audio_core_->SetRoutingPolicy(policy);
  };

 private:
  void NotifyGainMuteChanged();

  float system_gain_db_;
  bool system_muted_;

  void PublishServices();

  fidl::BindingSet<fuchsia::media::Audio> bindings_;

  fuchsia::media::AudioCorePtr audio_core_;
  fit::closure quit_callback_;
};
}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_AUDIO_CORE_CLIENT_H_
