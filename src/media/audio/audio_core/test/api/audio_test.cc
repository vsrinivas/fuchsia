// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>

#include "src/media/audio/lib/test/hermetic_audio_test.h"

namespace media::audio::test {

class AudioTest : public HermeticAudioTest {
 protected:
  fuchsia::media::AudioRendererPtr audio_renderer_;
};

// Test that the user is connected to the Audio FIDL service.
TEST_F(AudioTest, ConnectToAudioService) {
  fuchsia::media::AudioPtr audio_client;
  environment()->ConnectToService(audio_client.NewRequest());
  AddErrorHandler(audio_client, "AudioClient");

  audio_client->CreateAudioRenderer(audio_renderer_.NewRequest());
  audio_renderer_->GetMinLeadTime(AddCallback("GetMinLeadTime"));
  ExpectCallback();
}

}  // namespace media::audio::test
