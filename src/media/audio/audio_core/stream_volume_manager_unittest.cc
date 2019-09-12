// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/stream_volume_manager.h"

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/mixer/gain.h"

namespace media::audio {
namespace {

// Some absurd value that wouldn't be arrived at without consulting the value.
const float kDefaultAdjustedGain = -4500.0;

using namespace testing;

class MockStreamGain : public StreamGain {
 public:
  MockStreamGain() = default;

  float GetStreamGain() const override { return stream_gain_db_; }
  bool GetStreamMute() const override { return mute_; }
  fuchsia::media::Usage GetStreamUsage() const override { return fidl::Clone(usage_); }
  void RealizeAdjustedGain(float gain_db, std::optional<Ramp> ramp) override {
    adjusted_gain_db_ = gain_db;
    ramp_ = ramp;
  }

  bool mute_ = false;
  float stream_gain_db_ = Gain::kUnityGainDb;
  fuchsia::media::Usage usage_;
  float adjusted_gain_db_ = kDefaultAdjustedGain;
  std::optional<Ramp> ramp_;
};

TEST(StreamVolumeManagerTest, StreamCanUpdateSelf) {
  MockStreamGain mock;
  mock.usage_ = UsageFrom(fuchsia::media::AudioRenderUsage::INTERRUPTION);
  mock.stream_gain_db_ = -11.0;

  StreamVolumeManager manager;
  manager.NotifyStreamChanged(&mock);
  EXPECT_FLOAT_EQ(mock.adjusted_gain_db_, -11.0);
}

TEST(StreamVolumeManagerTest, ChangingUsageGainUpdatesRegisteredStreams) {
  MockStreamGain mock;
  mock.usage_ = UsageFrom(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT);

  StreamVolumeManager manager;
  manager.AddStream(&mock);
  manager.SetUsageGain(UsageFrom(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT), -10.0);

  EXPECT_FLOAT_EQ(mock.adjusted_gain_db_, -10.0);
}

TEST(StreamVolumeManagerTest, UsageGainIsClampedToUnity) {
  MockStreamGain mock;
  mock.usage_ = UsageFrom(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT);

  StreamVolumeManager manager;
  manager.AddStream(&mock);
  manager.SetUsageGain(UsageFrom(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT), 10.0);

  EXPECT_EQ(mock.adjusted_gain_db_, Gain::kUnityGainDb);
}

TEST(StreamVolumeManagerTest, StreamGainIsConsidered) {
  MockStreamGain mock;
  mock.stream_gain_db_ = -20.0;
  mock.usage_ = UsageFrom(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT);

  StreamVolumeManager manager;
  manager.AddStream(&mock);
  manager.SetUsageGain(UsageFrom(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT), 0.0);

  EXPECT_FLOAT_EQ(mock.adjusted_gain_db_, -20.0);
}

TEST(StreamVolumeManagerTest, StreamsCanBeRemoved) {
  MockStreamGain mock;
  mock.usage_ = UsageFrom(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT);

  StreamVolumeManager manager;
  manager.AddStream(&mock);
  manager.RemoveStream(&mock);
  manager.SetUsageGain(UsageFrom(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT), 10.0);

  EXPECT_FLOAT_EQ(mock.adjusted_gain_db_, kDefaultAdjustedGain);
}

TEST(StreamVolumeManagerTest, StreamsCanRamp) {
  MockStreamGain mock;
  mock.usage_ = UsageFrom(fuchsia::media::AudioRenderUsage::INTERRUPTION);
  mock.stream_gain_db_ = -11.0;

  StreamVolumeManager manager;
  manager.NotifyStreamChanged(&mock, Ramp{100, fuchsia::media::audio::RampType::SCALE_LINEAR});
  EXPECT_FLOAT_EQ(mock.adjusted_gain_db_, -11.0);
  EXPECT_EQ(mock.ramp_->duration_ns, 100);
  EXPECT_EQ(mock.ramp_->ramp_type, fuchsia::media::audio::RampType::SCALE_LINEAR);
}

TEST(StreamVolumeManagerTest, StreamMuteConsidered) {
  MockStreamGain mock;
  mock.usage_ = UsageFrom(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT);
  mock.mute_ = true;

  StreamVolumeManager manager;
  manager.AddStream(&mock);
  manager.SetUsageGain(UsageFrom(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT), 10.0);

  EXPECT_FLOAT_EQ(mock.adjusted_gain_db_, fuchsia::media::audio::MUTED_GAIN_DB);
}

}  // namespace
}  // namespace media::audio
