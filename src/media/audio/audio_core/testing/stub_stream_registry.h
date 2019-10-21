// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_STUB_STREAM_REGISTRY_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_STUB_STREAM_REGISTRY_H_

#include "src/media/audio/audio_core/stream_registry.h"

namespace media::audio::testing {

class StubStreamRegistry : public StreamRegistry {
 public:
  ~StubStreamRegistry() override = default;

  // |media::audio::StreamRegistry|
  void AddAudioRenderer(fbl::RefPtr<AudioRendererImpl> audio_renderer) override {}
  void RemoveAudioRenderer(AudioRendererImpl* audio_renderer) override {}
  void AddAudioCapturer(const fbl::RefPtr<AudioCapturerImpl>& audio_capturer) override {}
  void RemoveAudioCapturer(AudioCapturerImpl* audio_capturer) override {}
};

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_STUB_STREAM_REGISTRY_H_
