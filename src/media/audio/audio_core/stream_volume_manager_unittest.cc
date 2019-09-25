// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/stream_volume_manager.h"

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/mixer/gain.h"

namespace media::audio {
namespace {

using namespace testing;

class MockStreamVolume : public StreamVolume {
 public:
  MockStreamVolume() = default;

  bool GetStreamMute() const override { return mute_; }
  fuchsia::media::Usage GetStreamUsage() const override { return fidl::Clone(usage_); }
  void RealizeVolume(VolumeCommand volume_command) override { volume_command_ = volume_command; }

  bool mute_ = false;
  fuchsia::media::Usage usage_;
  VolumeCommand volume_command_ = {};
};

TEST(StreamVolumeManagerTest, StreamCanUpdateSelf) {
  MockStreamVolume mock;
  mock.usage_ = UsageFrom(fuchsia::media::AudioRenderUsage::INTERRUPTION);

  StreamVolumeManager manager;
  manager.NotifyStreamChanged(&mock);
  EXPECT_FLOAT_EQ(mock.volume_command_.volume, 1.0);
  EXPECT_FLOAT_EQ(mock.volume_command_.gain_db_adjustment, Gain::kUnityGainDb);
  EXPECT_EQ(mock.volume_command_.ramp, std::nullopt);
}

TEST(StreamVolumeManagerTest, UsageChangesUpdateRegisteredStreams) {
  MockStreamVolume mock;
  mock.usage_ = UsageFrom(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT);

  StreamVolumeManager manager;
  manager.AddStream(&mock);
  manager.SetUsageGain(UsageFrom(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT), -10.0);

  EXPECT_FLOAT_EQ(mock.volume_command_.gain_db_adjustment, -10.0);
}

TEST(StreamVolumeManagerTest, StreamMuteIsConsidered) {
  MockStreamVolume mock;
  mock.mute_ = true;
  mock.usage_ = UsageFrom(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT);

  StreamVolumeManager manager;
  manager.AddStream(&mock);
  manager.SetUsageGain(UsageFrom(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT), 0.0);

  EXPECT_EQ(mock.volume_command_.gain_db_adjustment, fuchsia::media::audio::MUTED_GAIN_DB);
}

TEST(StreamVolumeManagerTest, StreamsCanBeRemoved) {
  MockStreamVolume mock;
  mock.usage_ = UsageFrom(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT);

  StreamVolumeManager manager;
  manager.AddStream(&mock);
  manager.RemoveStream(&mock);
  manager.SetUsageGain(UsageFrom(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT), 10.0);

  EXPECT_FLOAT_EQ(mock.volume_command_.volume, 0.0);
  EXPECT_FLOAT_EQ(mock.volume_command_.gain_db_adjustment, Gain::kUnityGainDb);
  EXPECT_EQ(mock.volume_command_.ramp, std::nullopt);
}

TEST(StreamVolumeManagerTest, StreamsCanRamp) {
  MockStreamVolume mock;
  mock.usage_ = UsageFrom(fuchsia::media::AudioRenderUsage::INTERRUPTION);

  StreamVolumeManager manager;
  manager.NotifyStreamChanged(&mock, Ramp{100, fuchsia::media::audio::RampType::SCALE_LINEAR});

  EXPECT_EQ(mock.volume_command_.ramp->duration_ns, 100);
  EXPECT_EQ(mock.volume_command_.ramp->ramp_type, fuchsia::media::audio::RampType::SCALE_LINEAR);
}

}  // namespace
}  // namespace media::audio
