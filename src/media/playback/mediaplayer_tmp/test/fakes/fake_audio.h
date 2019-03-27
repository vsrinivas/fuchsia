// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_TEST_FAKES_FAKE_AUDIO_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_TEST_FAKES_FAKE_AUDIO_H_

#include <fuchsia/media/cpp/fidl.h>
#include <memory>
#include <queue>
#include <vector>
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/logging.h"
#include "src/media/playback/mediaplayer_tmp/test/fakes/fake_audio_renderer.h"

namespace media_player {
namespace test {

// Implements Audio for testing.
class FakeAudio : public fuchsia::media::Audio {
 public:
  FakeAudio() = default;

  ~FakeAudio() override = default;

  // Returns a request handler for binding to this fake service.
  fidl::InterfaceRequestHandler<fuchsia::media::Audio> GetRequestHandler() {
    return bindings_.GetHandler(this);
  }

  FakeAudioRenderer& renderer() { return fake_audio_renderer_; }

  // Audio implementation.
  void CreateAudioRenderer(
      ::fidl::InterfaceRequest<fuchsia::media::AudioRenderer>
          audio_renderer_request) override {
    fake_audio_renderer_.Bind(std::move(audio_renderer_request));
  }

  void CreateAudioCapturer(
      ::fidl::InterfaceRequest<fuchsia::media::AudioCapturer>
          audio_capturer_request,
      bool loopback) override {
    FXL_NOTIMPLEMENTED();
  }

  void SetSystemGain(float gain_db) override { FXL_NOTIMPLEMENTED(); }

  void SetSystemMute(bool muted) override { FXL_NOTIMPLEMENTED(); }

  void SetRoutingPolicy(
      fuchsia::media::AudioOutputRoutingPolicy policy) override {
    FXL_NOTIMPLEMENTED();
  }

 private:
  fidl::BindingSet<fuchsia::media::Audio> bindings_;
  FakeAudioRenderer fake_audio_renderer_;
};

}  // namespace test
}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_TMP_TEST_FAKES_FAKE_AUDIO_H_
