// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/volume_control.h"

#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/gtest/test_loop_fixture.h>

#include <gtest/gtest.h>

namespace media::audio {
namespace {

// TODO(turnage): Move to FIDL
constexpr zx_status_t kBacklogFullEpitaph = 88;

class MockVolumeSetting : public VolumeSetting {
 public:
  void SetVolume(float volume) override { volume_ = volume; }

  float volume() const { return volume_; }

 private:
  float volume_;
};

class VolumeControlTest : public ::gtest::TestLoopFixture {
 protected:
  VolumeControlTest() : volume_control_(&setting_, dispatcher()) {}

  fuchsia::media::audio::VolumeControlPtr BindVolumeControl() {
    fuchsia::media::audio::VolumeControlPtr volume_control_ptr;
    volume_control_.AddBinding(volume_control_ptr.NewRequest(dispatcher()));
    return volume_control_ptr;
  }

  MockVolumeSetting setting_;
  VolumeControl volume_control_;
};

TEST_F(VolumeControlTest, SetsVolume) {
  auto client = BindVolumeControl();

  client->SetVolume(0.5);
  RunLoopUntilIdle();
  EXPECT_FLOAT_EQ(setting_.volume(), 0.5);
}

TEST_F(VolumeControlTest, SetsMute) {
  auto client = BindVolumeControl();

  client->SetVolume(0.5);
  RunLoopUntilIdle();
  EXPECT_FLOAT_EQ(setting_.volume(), 0.5);

  client->SetMute(true);
  RunLoopUntilIdle();
  EXPECT_FLOAT_EQ(setting_.volume(), 0.0);

  // On unmute, volume should restore.
  client->SetMute(false);
  RunLoopUntilIdle();
  EXPECT_FLOAT_EQ(setting_.volume(), 0.5);
}

TEST_F(VolumeControlTest, MultipleClients) {
  auto client1 = BindVolumeControl();
  auto client2 = BindVolumeControl();

  client1->SetVolume(0.1);
  RunLoopUntilIdle();
  EXPECT_FLOAT_EQ(setting_.volume(), 0.1);

  client2->SetVolume(0.4);
  RunLoopUntilIdle();
  EXPECT_FLOAT_EQ(setting_.volume(), 0.4);
}

TEST_F(VolumeControlTest, SetVolumeDoesNotUnmute) {
  auto client = BindVolumeControl();

  client->SetVolume(0.1);
  RunLoopUntilIdle();
  EXPECT_FLOAT_EQ(setting_.volume(), 0.1);

  client->SetMute(true);
  RunLoopUntilIdle();
  EXPECT_FLOAT_EQ(setting_.volume(), 0.0);

  client->SetVolume(0.8);
  RunLoopUntilIdle();
  EXPECT_FLOAT_EQ(setting_.volume(), 0.0);

  client->SetMute(false);
  RunLoopUntilIdle();
  EXPECT_FLOAT_EQ(setting_.volume(), 0.8);
}

TEST_F(VolumeControlTest, ClientEvents) {
  float volume;
  bool muted;
  auto client = BindVolumeControl();
  client.events().OnVolumeMuteChanged = [&client, &volume, &muted](float new_volume,
                                                                   bool new_muted) {
    client->NotifyVolumeMuteChangedHandled();
    volume = new_volume;
    muted = new_muted;
  };

  client->SetVolume(0.1);
  RunLoopUntilIdle();
  EXPECT_FLOAT_EQ(volume, 0.1);
  EXPECT_FALSE(muted);

  client->SetMute(true);
  RunLoopUntilIdle();
  EXPECT_FLOAT_EQ(volume, 0.1);
  EXPECT_TRUE(muted);

  client->SetVolume(0.8);
  RunLoopUntilIdle();
  EXPECT_FLOAT_EQ(volume, 0.8);
  EXPECT_TRUE(muted);

  client->SetMute(false);
  RunLoopUntilIdle();
  EXPECT_FLOAT_EQ(volume, 0.8);
  EXPECT_FALSE(muted);
}

TEST_F(VolumeControlTest, DuplicateSetsGenerateNoEvents) {
  float volume;
  bool muted;
  size_t event_count = 0;
  auto client = BindVolumeControl();
  client.events().OnVolumeMuteChanged = [&client, &event_count, &volume, &muted](float new_volume,
                                                                                 bool new_muted) {
    client->NotifyVolumeMuteChangedHandled();
    volume = new_volume;
    muted = new_muted;
    ++event_count;
  };

  client->SetVolume(0.1);
  client->SetVolume(0.1);
  RunLoopUntilIdle();
  EXPECT_FLOAT_EQ(volume, 0.1);
  EXPECT_FALSE(muted);
  EXPECT_EQ(event_count, 1u);

  event_count = 0;
  client->SetMute(true);
  client->SetMute(true);
  RunLoopUntilIdle();
  EXPECT_TRUE(muted);
  EXPECT_EQ(event_count, 1u);
}

TEST_F(VolumeControlTest, AllClientsReceiveEvents) {
  float volume1;
  bool muted1;
  auto client1 = BindVolumeControl();
  client1.events().OnVolumeMuteChanged = [&client1, &volume1, &muted1](float new_volume,
                                                                       bool new_muted) {
    client1->NotifyVolumeMuteChangedHandled();
    volume1 = new_volume;
    muted1 = new_muted;
  };

  float volume2;
  bool muted2;
  auto client2 = BindVolumeControl();
  client2.events().OnVolumeMuteChanged = [&client2, &volume2, &muted2](float new_volume,
                                                                       bool new_muted) {
    client2->NotifyVolumeMuteChangedHandled();
    volume2 = new_volume;
    muted2 = new_muted;
  };

  client1->SetVolume(0.1);
  RunLoopUntilIdle();
  EXPECT_FLOAT_EQ(volume1, 0.1);
  EXPECT_FALSE(muted1);
  EXPECT_FLOAT_EQ(volume2, 0.1);
  EXPECT_FALSE(muted2);
}

TEST_F(VolumeControlTest, ClientDisconnectsIfAckLimitExceeded) {
  size_t client1_event_count = 0;
  auto client1 = BindVolumeControl();
  client1.events().OnVolumeMuteChanged = [&client1, &client1_event_count](float _volume,
                                                                          bool _muted) {
    client1->NotifyVolumeMuteChangedHandled();
    client1_event_count += 1;
  };

  auto client2 = BindVolumeControl();
  zx_status_t client2_error = ZX_OK;
  client2.set_error_handler([&client2_error](zx_status_t error) { client2_error = error; });

  for (size_t i = 0; i < VolumeControl::kMaxEventsSentWithoutAck + 1; ++i) {
    client1->SetVolume(static_cast<float>(i) / 100.0);
    RunLoopUntilIdle();
  }

  EXPECT_EQ(client1_event_count, VolumeControl::kMaxEventsSentWithoutAck + 1);
  EXPECT_EQ(client2_error, kBacklogFullEpitaph);
}

}  // namespace
}  // namespace media::audio
