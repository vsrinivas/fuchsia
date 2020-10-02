// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_IMPL_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_IMPL_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include "src/media/audio/audio_core/audio_core_impl.h"

namespace media::audio {

class AudioImpl : public fuchsia::media::Audio {
 public:
  explicit AudioImpl(Context* context) : audio_core_(context) {}

  fidl::InterfaceRequestHandler<fuchsia::media::Audio> GetFidlRequestHandler() {
    return bindings_.GetHandler(this);
  }

  // |fuchsia::media::Audio|
  void CreateAudioRenderer(
      fidl::InterfaceRequest<fuchsia::media::AudioRenderer> audio_renderer_request) final {
    audio_core_.CreateAudioRenderer(std::move(audio_renderer_request));
  }
  void CreateAudioCapturer(
      fidl::InterfaceRequest<fuchsia::media::AudioCapturer> audio_capturer_request,
      bool loopback) final {
    audio_core_.CreateAudioCapturer(loopback, std::move(audio_capturer_request));
  }

 private:
  fidl::BindingSet<fuchsia::media::Audio> bindings_;

  AudioCoreImpl audio_core_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_AUDIO_IMPL_H_
