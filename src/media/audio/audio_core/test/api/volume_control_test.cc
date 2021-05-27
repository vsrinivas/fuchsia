// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>

#include <cmath>

#include "src/media/audio/lib/test/hermetic_audio_test.h"

using AudioCaptureUsage = fuchsia::media::AudioCaptureUsage;
using AudioRenderUsage = fuchsia::media::AudioRenderUsage;

namespace media::audio::test {

class VolumeControlTest : public HermeticAudioTest {
 protected:
  fuchsia::media::audio::VolumeControlPtr CreateRenderUsageControl(AudioRenderUsage u) {
    fuchsia::media::audio::VolumeControlPtr c;
    audio_core_->BindUsageVolumeControl(fuchsia::media::Usage::WithRenderUsage(std::move(u)),
                                        c.NewRequest());
    AddErrorHandler(c, "VolumeControl");
    return c;
  }
};

TEST_F(VolumeControlTest, SetVolumeAndMute) {
  auto client1 = CreateRenderUsageControl(AudioRenderUsage::MEDIA);
  auto client2 = CreateRenderUsageControl(AudioRenderUsage::MEDIA);

  float volume = 0.0;
  bool muted = false;
  auto add_callback = [this, &client2, &volume, &muted]() {
    client2.events().OnVolumeMuteChanged =
        AddCallback("OnVolumeMuteChanged", [&volume, &muted](float new_volume, bool new_muted) {
          volume = new_volume;
          muted = new_muted;
        });
  };

  // The initial callback happens immediately.
  add_callback();
  ExpectCallbacks();
  EXPECT_FLOAT_EQ(volume, 1.0);
  EXPECT_EQ(muted, false);

  // Further callbacks happen in response to events.
  add_callback();
  client1->SetVolume(0.5);
  ExpectCallbacks();
  EXPECT_FLOAT_EQ(volume, 0.5);
  EXPECT_EQ(muted, false);

  add_callback();
  client1->SetMute(true);
  ExpectCallbacks();
  EXPECT_EQ(muted, true);

  // Unmute should restore the volume.
  add_callback();
  client1->SetMute(false);
  ExpectCallbacks();
  EXPECT_FLOAT_EQ(volume, 0.5);
  EXPECT_EQ(muted, false);
}

TEST_F(VolumeControlTest, RoutedCorrectly) {
  auto c1 = CreateRenderUsageControl(AudioRenderUsage::MEDIA);
  auto c2 = CreateRenderUsageControl(AudioRenderUsage::BACKGROUND);

  // The initial callbacks happen immediately.
  c1.events().OnVolumeMuteChanged = AddCallback("OnVolumeMuteChanged1 InitialCall");
  c2.events().OnVolumeMuteChanged = AddCallback("OnVolumeMuteChanged2 InitialCall");
  ExpectCallbacks();

  // Routing to c1.
  c1.events().OnVolumeMuteChanged = AddCallback("OnVolumeMuteChanged1 RouteTo1");
  c2.events().OnVolumeMuteChanged = AddUnexpectedCallback("OnVolumeMuteChanged2 RouteTo1");
  c1->SetVolume(0);
  ExpectCallbacks();

  // Routing to c2.
  c1.events().OnVolumeMuteChanged = AddUnexpectedCallback("OnVolumeMuteChanged1 RouteTo2");
  c2.events().OnVolumeMuteChanged = AddCallback("OnVolumeMuteChanged2 RouteTo2");
  c2->SetVolume(0);
  ExpectCallbacks();
}

TEST_F(VolumeControlTest, FailToConnectToCaptureUsageVolume) {
  fuchsia::media::audio::VolumeControlPtr client;
  audio_core_->BindUsageVolumeControl(fidl::Clone(fuchsia::media::Usage::WithCaptureUsage(
                                          fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)),
                                      client.NewRequest());
  AddErrorHandler(client, "VolumeControl");

  ExpectError(client, ZX_ERR_NOT_SUPPORTED);
}

}  // namespace media::audio::test
