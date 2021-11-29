// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_FAKES_FAKE_AUDIO_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_FAKES_FAKE_AUDIO_H_

#include <fuchsia/media/cpp/fidl.h>

#include <memory>
#include <queue>
#include <vector>

#include "lib/fidl/cpp/binding_set.h"
#include "src/media/playback/mediaplayer/test/fakes/fake_audio_renderer.h"

namespace media_player {
namespace test {

// Implements Audio for testing.
class FakeAudio : public fuchsia::media::Audio {
 public:
  // Returns a request handler for binding to this fake service.
  fidl::InterfaceRequestHandler<fuchsia::media::Audio> GetRequestHandler() {
    return bindings_.GetHandler(this);
  }

  FakeAudioRenderer& renderer() { return fake_audio_renderer_; }

  bool create_audio_renderer_called() const { return create_audio_renderer_called_; }

  // Audio implementation.
  void CreateAudioRenderer(
      fidl::InterfaceRequest<fuchsia::media::AudioRenderer> audio_renderer_request) override {
    fake_audio_renderer_.Bind(std::move(audio_renderer_request));
    create_audio_renderer_called_ = true;
  }

  void CreateAudioCapturer(
      fidl::InterfaceRequest<fuchsia::media::AudioCapturer> audio_capturer_request,
      bool loopback) override {
    FX_NOTIMPLEMENTED();
  }

 private:
  fidl::BindingSet<fuchsia::media::Audio> bindings_;
  FakeAudioRenderer fake_audio_renderer_;
  bool create_audio_renderer_called_ = false;
};

class FakeAudioCore : public fuchsia::media::AudioCore {
 public:
  FakeAudioCore() { fake_audio_renderers_.emplace_back(std::make_unique<FakeAudioRenderer>()); }
  // Returns a request handler for binding to this fake service.
  fidl::InterfaceRequestHandler<fuchsia::media::AudioCore> GetRequestHandler() {
    return bindings_.GetHandler(this);
  }

  // Returns default/first renderer to be created
  FakeAudioRenderer& renderer() { return *fake_audio_renderers_.front(); }

  // Audio implementation.
  void CreateAudioRenderer(
      fidl::InterfaceRequest<fuchsia::media::AudioRenderer> audio_renderer_request) override {
    // Some tests create multiple renderers, so support that, although we don't currently expose a
    // way to expect packets on the later ones.
    if (fake_audio_renderers_.front()->is_bound()) {
      fake_audio_renderers_.emplace_back(std::make_unique<FakeAudioRenderer>());
    }
    fake_audio_renderers_.back()->Bind(std::move(audio_renderer_request));
  }

  void CreateAudioCapturerWithConfiguration(
      ::fuchsia::media::AudioStreamType stream_type,
      ::fuchsia::media::AudioCapturerConfiguration configuration,
      ::fidl::InterfaceRequest<::fuchsia::media::AudioCapturer> audio_capturer_request) override {}

  void CreateAudioCapturer(
      bool loopback,
      ::fidl::InterfaceRequest<::fuchsia::media::AudioCapturer> audio_in_request) override {}

  void EnableDeviceSettings(bool enabled) override {}

  void SetRenderUsageGain(::fuchsia::media::AudioRenderUsage usage, float gain_db) override {}

  void SetCaptureUsageGain(::fuchsia::media::AudioCaptureUsage usage, float gain_db) override {}

  void BindUsageVolumeControl(
      ::fuchsia::media::Usage usage,
      ::fidl::InterfaceRequest<::fuchsia::media::audio::VolumeControl> volume_control) override {}

  void GetVolumeFromDb(::fuchsia::media::Usage usage, float gain_db,
                       GetVolumeFromDbCallback callback) override {
    // Value to check in test
    callback(0.5f);
  }

  void GetDbFromVolume(::fuchsia::media::Usage usage, float volume,
                       GetDbFromVolumeCallback callback) override {
    // Value to check in test
    callback(-20.0f);
  }

  void SetInteraction(::fuchsia::media::Usage active, ::fuchsia::media::Usage affected,
                      ::fuchsia::media::Behavior behavior) override {}

  void ResetInteractions() override {}

  void LoadDefaults() override {}

 private:
  fidl::BindingSet<fuchsia::media::AudioCore> bindings_;
  std::vector<std::unique_ptr<FakeAudioRenderer>> fake_audio_renderers_;
};

}  // namespace test
}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TEST_FAKES_FAKE_AUDIO_H_
