// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/media/tuning/cpp/fidl.h>
#include <fuchsia/virtualaudio/cpp/fidl.h>

#include <cmath>

#include "src/media/audio/lib/test/hermetic_audio_test.h"

namespace media::audio::test {

class VolumeControlTest : public HermeticAudioTest {};

// TODO(52962): Flesh out
TEST_F(VolumeControlTest, ConnectToRenderUsageVolume) {
  fuchsia::media::AudioCorePtr audio_core;
  environment()->ConnectToService(audio_core.NewRequest());
  audio_core.set_error_handler(ErrorHandler());

  fuchsia::media::audio::VolumeControlPtr client1;
  fuchsia::media::audio::VolumeControlPtr client2;

  fuchsia::media::Usage usage;
  usage.set_render_usage(fuchsia::media::AudioRenderUsage::MEDIA);

  audio_core->BindUsageVolumeControl(fidl::Clone(usage), client1.NewRequest());
  audio_core->BindUsageVolumeControl(fidl::Clone(usage), client2.NewRequest());

  float volume = 0.0;
  bool muted = false;
  client2.events().OnVolumeMuteChanged =
      CompletionCallback([&volume, &muted](float new_volume, bool new_muted) {
        volume = new_volume;
        muted = new_muted;
      });

  ExpectCallback();
  EXPECT_FLOAT_EQ(volume, 1.0);

  client1->SetVolume(0.5);
  ExpectCallback();
  EXPECT_FLOAT_EQ(volume, 0.5);
  EXPECT_EQ(muted, false);

  client1->SetMute(true);
  ExpectCallback();
  EXPECT_EQ(muted, true);
}

TEST_F(VolumeControlTest, FailToConnectToCaptureUsageVolume) {
  fuchsia::media::Usage usage;
  usage.set_capture_usage(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT);

  std::optional<zx_status_t> client_error;
  fuchsia::media::audio::VolumeControlPtr client;
  client.set_error_handler([&client_error](zx_status_t status) { client_error = status; });

  audio_core_->BindUsageVolumeControl(fidl::Clone(usage), client.NewRequest());
  RunLoopUntil([&client_error] { return client_error != std::nullopt; });

  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, *client_error);
}

}  // namespace media::audio::test
