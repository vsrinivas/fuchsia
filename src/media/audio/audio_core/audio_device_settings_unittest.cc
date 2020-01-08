// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_device_settings.h"

#include <lib/gtest/test_loop_fixture.h>

#include "src/media/audio/audio_core/audio_driver.h"

namespace media::audio {
namespace {

class CallbackCounter {
 public:
  fit::function<void(const AudioDeviceSettings*)> TakeObserver() { return std::move(observer_); }

  size_t callback_count() const { return callback_count_; }

 private:
  fit::function<void(const AudioDeviceSettings*)> observer_{[this](...) { ++callback_count_; }};
  size_t callback_count_ = 0;
};

static constexpr audio_stream_unique_id_t kTestUniqueId = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}};

static constexpr HwGainState kDefaultInitialHwGainState = {
    false,   /* cur_mute */
    false,   /* cur_agc */
    0.0,     /* cur_gain */
    true,    /* can_mute */
    true,    /* can_agc */
    -160.0f, /* min_gain */
    24.0f,   /* max_gain */
    1.0f     /* gain_step */
};

class AudioDeviceSettingsTest : public gtest::TestLoopFixture {};

// If agc is not supported, then always return 'false' for agc enabled.
TEST_F(AudioDeviceSettingsTest, AgcFalseWhenNotSupported) {
  // Set AGC/Mute to 'true' but not supported.
  HwGainState hw_gain_state = kDefaultInitialHwGainState;
  hw_gain_state.cur_agc = true;
  hw_gain_state.can_agc = false;
  AudioDeviceSettings settings(kTestUniqueId, hw_gain_state, false);

  fuchsia::media::AudioGainInfo gain_info;
  settings.GetGainInfo(&gain_info);

  EXPECT_FALSE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcEnabled);
  EXPECT_FALSE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcSupported);
}

// If can_mute is false, still allow the device to be muted. In cases without the hardware mute
// we'll implement mute in software.
TEST_F(AudioDeviceSettingsTest, MuteTrueWhenNotSupported) {
  // Set AGC/Mute to 'true' but not supported.
  HwGainState hw_gain_state = kDefaultInitialHwGainState;
  hw_gain_state.cur_mute = true;
  hw_gain_state.can_mute = false;
  AudioDeviceSettings settings(kTestUniqueId, hw_gain_state, false);

  fuchsia::media::AudioGainInfo gain_info;
  settings.GetGainInfo(&gain_info);

  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_Mute);
}

TEST_F(AudioDeviceSettingsTest, SetGainInfoDoesNothingWithNoFlags) {
  CallbackCounter counter;
  // Set AGC/Mute to 'true' but not supported.
  HwGainState hw_gain_state = kDefaultInitialHwGainState;
  hw_gain_state.cur_mute = true;
  hw_gain_state.cur_agc = true;
  hw_gain_state.can_mute = true;
  hw_gain_state.can_agc = true;
  hw_gain_state.cur_gain = 5.0;
  AudioDeviceSettings settings(kTestUniqueId, hw_gain_state, false);
  settings.set_observer(counter.TakeObserver());

  // Verify initial state.
  fuchsia::media::AudioGainInfo gain_info;
  settings.GetGainInfo(&gain_info);
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_Mute);
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcEnabled);
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcSupported);
  EXPECT_EQ(5.0f, gain_info.gain_db);

  // Update gain state
  fuchsia::media::AudioGainInfo new_gain_info;
  new_gain_info.gain_db = 10.0;
  new_gain_info.flags = 0;
  EXPECT_FALSE(settings.SetGainInfo(new_gain_info, 0 /* flags */));

  // State should match initial state.
  settings.GetGainInfo(&gain_info);
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_Mute);
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcEnabled);
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcSupported);
  EXPECT_EQ(5.0f, gain_info.gain_db);

  // Nothing should have changed, so the observer should not have been notified.
  EXPECT_EQ(0u, counter.callback_count());
}

TEST_F(AudioDeviceSettingsTest, SetGainInfoOnlyGainDb) {
  CallbackCounter counter;
  // Set AGC/Mute to 'true' but not supported.
  HwGainState hw_gain_state = kDefaultInitialHwGainState;
  hw_gain_state.cur_mute = true;
  hw_gain_state.cur_agc = true;
  hw_gain_state.can_mute = true;
  hw_gain_state.can_agc = true;
  hw_gain_state.cur_gain = 5.0;
  AudioDeviceSettings settings(kTestUniqueId, hw_gain_state, false);
  settings.set_observer(counter.TakeObserver());

  // Verify initial state.
  fuchsia::media::AudioGainInfo gain_info;
  settings.GetGainInfo(&gain_info);
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_Mute);
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcEnabled);
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcSupported);
  EXPECT_EQ(5.0f, gain_info.gain_db);

  // Update gain state
  fuchsia::media::AudioGainInfo new_gain_info;
  new_gain_info.gain_db = 10.0;
  new_gain_info.flags = 0;
  EXPECT_TRUE(settings.SetGainInfo(new_gain_info, fuchsia::media::SetAudioGainFlag_GainValid));

  // Only gain updated.
  settings.GetGainInfo(&gain_info);
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_Mute);
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcEnabled);
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcSupported);
  EXPECT_EQ(10.0f, gain_info.gain_db);
  EXPECT_EQ(1u, counter.callback_count());
}

TEST_F(AudioDeviceSettingsTest, SetGainInfoOnlyMute) {
  CallbackCounter counter;
  // Set AGC/Mute to 'true' but not supported.
  HwGainState hw_gain_state = kDefaultInitialHwGainState;
  hw_gain_state.cur_mute = true;
  hw_gain_state.cur_agc = true;
  hw_gain_state.can_mute = true;
  hw_gain_state.can_agc = true;
  hw_gain_state.cur_gain = 5.0;
  AudioDeviceSettings settings(kTestUniqueId, hw_gain_state, false);
  settings.set_observer(counter.TakeObserver());

  // Verify initial state.
  fuchsia::media::AudioGainInfo gain_info;
  settings.GetGainInfo(&gain_info);
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_Mute);
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcEnabled);
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcSupported);
  EXPECT_EQ(5.0f, gain_info.gain_db);

  // Update gain state
  fuchsia::media::AudioGainInfo new_gain_info;
  new_gain_info.gain_db = 10.0;
  new_gain_info.flags = 0;
  EXPECT_TRUE(settings.SetGainInfo(new_gain_info, fuchsia::media::SetAudioGainFlag_MuteValid));

  // Only gain updated.
  settings.GetGainInfo(&gain_info);
  EXPECT_FALSE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_Mute);
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcEnabled);
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcSupported);
  EXPECT_EQ(5.0f, gain_info.gain_db);
  EXPECT_EQ(1u, counter.callback_count());
}

TEST_F(AudioDeviceSettingsTest, SetGainInfoOnlyAgc) {
  CallbackCounter counter;
  // Set AGC/Mute to 'true' but not supported.
  HwGainState hw_gain_state = kDefaultInitialHwGainState;
  hw_gain_state.cur_mute = true;
  hw_gain_state.cur_agc = true;
  hw_gain_state.can_mute = true;
  hw_gain_state.can_agc = true;
  hw_gain_state.cur_gain = 5.0;
  AudioDeviceSettings settings(kTestUniqueId, hw_gain_state, false);
  settings.set_observer(counter.TakeObserver());

  // Verify initial state.
  fuchsia::media::AudioGainInfo gain_info;
  settings.GetGainInfo(&gain_info);
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_Mute);
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcEnabled);
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcSupported);
  EXPECT_EQ(5.0f, gain_info.gain_db);

  // Update gain state
  fuchsia::media::AudioGainInfo new_gain_info;
  new_gain_info.gain_db = 10.0;
  new_gain_info.flags = 0;
  EXPECT_TRUE(settings.SetGainInfo(new_gain_info, fuchsia::media::SetAudioGainFlag_AgcValid));

  // Only gain updated.
  settings.GetGainInfo(&gain_info);
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_Mute);
  EXPECT_FALSE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcEnabled);
  // Note that we expect AgcSupported to remain unchanged.
  EXPECT_TRUE(gain_info.flags & fuchsia::media::AudioGainInfoFlag_AgcSupported);
  EXPECT_EQ(5.0f, gain_info.gain_db);
  EXPECT_EQ(1u, counter.callback_count());
}

TEST_F(AudioDeviceSettingsTest, Clone) {
  HwGainState hw_gain_state = kDefaultInitialHwGainState;
  hw_gain_state.cur_mute = true;
  hw_gain_state.cur_agc = true;
  hw_gain_state.can_mute = true;
  hw_gain_state.can_agc = true;
  hw_gain_state.cur_gain = 5.0;
  AudioDeviceSettings settings(kTestUniqueId, hw_gain_state, false);
  settings.SetIgnored(settings.Ignored());

  auto clone = settings.Clone();

  fuchsia::media::AudioGainInfo gain_info, clone_gain_info;
  settings.GetGainInfo(&gain_info);
  clone->GetGainInfo(&clone_gain_info);

  EXPECT_EQ(gain_info.flags, clone_gain_info.flags);
  EXPECT_EQ(gain_info.gain_db, clone_gain_info.gain_db);
  EXPECT_EQ(settings.Ignored(), clone->Ignored());
  EXPECT_EQ(settings.AutoRoutingDisabled(), clone->AutoRoutingDisabled());
  EXPECT_EQ(settings.is_input(), clone->is_input());
  EXPECT_EQ(0, memcmp(settings.uid().data, clone->uid().data, sizeof(settings.uid().data)));
}

}  // namespace
}  // namespace media::audio
