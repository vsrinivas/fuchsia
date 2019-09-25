// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_OBJECT_REGISTRY_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_OBJECT_REGISTRY_H_

#include "src/media/audio/audio_core/object_registry.h"

namespace media::audio::testing {

class FakeObjectRegistry : public ObjectRegistry {
 public:
  ~FakeObjectRegistry() override = default;

  // |media::audio::ObjectRegistry|
  void AddAudioRenderer(fbl::RefPtr<AudioRendererImpl> audio_renderer) override {}
  void RemoveAudioRenderer(AudioRendererImpl* audio_renderer) override {}
  void AddAudioCapturer(const fbl::RefPtr<AudioCapturerImpl>& audio_capturer) override {}
  void RemoveAudioCapturer(AudioCapturerImpl* audio_capturer) override {}
  void AddDevice(const fbl::RefPtr<AudioDevice>& device) override {}
  void ActivateDevice(const fbl::RefPtr<AudioDevice>& device) override {}
  void RemoveDevice(const fbl::RefPtr<AudioDevice>& device) override {}
  void OnPlugStateChanged(const fbl::RefPtr<AudioDevice>& device, bool plugged,
                          zx_time_t plug_time) override {}
};

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TESTING_FAKE_OBJECT_REGISTRY_H_
