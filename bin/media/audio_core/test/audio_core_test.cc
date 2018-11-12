// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>

#include <lib/gtest/real_loop_fixture.h>

#include "garnet/bin/media/audio_core/test/audio_core_tests_shared.h"
#include "lib/component/cpp/environment_services_helper.h"
#include "lib/fxl/logging.h"

namespace media {
namespace audio {
namespace test {

//
// AudioCoreTest class
//
class AudioCoreTest : public gtest::RealLoopFixture {
 protected:
  virtual void SetUp() override;
  void TearDown() override;

  bool ReceiveNoDisconnectCallback();

  std::shared_ptr<component::Services> environment_services_;
  fuchsia::media::AudioPtr audio_;
  fuchsia::media::AudioRendererPtr audio_renderer_;
  fuchsia::media::AudioCapturerPtr audio_capturer_;

  bool error_occurred_ = false;
};

//
// SystemGainMuteTest class
//
class SystemGainMuteTest : public AudioCoreTest {
 protected:
  void SetUp() override;

  void PresetSystemGainMute();
  void SetSystemGain(float gain_db);
  void SetSystemMute(bool mute);
  bool ReceiveGainCallback(float gain_db, bool mute);
  bool ReceiveNoGainCallback();

  float received_gain_db_;
  bool received_mute_;
  bool received_gain_callback_ = false;
};

//
// AudioCoreTest implementation
//
// Connect to Audio interface and set an error handler
void AudioCoreTest::SetUp() {
  ::gtest::RealLoopFixture::SetUp();

  environment_services_ = component::GetEnvironmentServices();
  environment_services_->ConnectToService(audio_.NewRequest());
  ASSERT_TRUE(audio_);

  audio_.set_error_handler([this](zx_status_t error) {
    error_occurred_ = true;
    QuitLoop();
  });
}

void AudioCoreTest::TearDown() {
  EXPECT_FALSE(error_occurred_);

  ::gtest::RealLoopFixture::TearDown();
}

// Expecting NOT to receive a disconnect. Wait, then check for errors.
bool AudioCoreTest::ReceiveNoDisconnectCallback() {
  bool timed_out = RunLoopWithTimeout(kDurationTimeoutExpected);
  EXPECT_FALSE(error_occurred_);
  EXPECT_TRUE(timed_out) << kNoTimeoutErr;

  return !error_occurred_ && timed_out;
}

//
// SystemGainMuteTest implementation
//
// Register for notification of SystemGainMute changes; receive initial values
// and set the system to a known baseline for gain/mute testing.
void SystemGainMuteTest::SetUp() {
  AudioCoreTest::SetUp();

  audio_.events().SystemGainMuteChanged = [this](float gain_db, bool muted) {
    received_gain_db_ = gain_db;
    received_mute_ = muted;
    received_gain_callback_ = true;
    QuitLoop();
  };

  // When clients connects to Audio, the system enqueues an action to send the
  // newly-connected client a callback with the systemwide Gain|Mute settings.
  // The system executes this action after the client's currently executing task
  // completes. This means that if a client establishes a connection and then
  // registers a SystemGainMuteChanged callback BEFORE returning, this client
  // will subsequently (once the system gets a chance to run) receive an initial
  // notification of Gain|Mute settings at the time of connection. Conversely,
  // if a client DOES return before registering, even after subsequently
  // registering for the event the client has no way of learning the current
  // Gain|Mute settings until they are changed.
  bool timed_out = RunLoopWithTimeout(kDurationResponseExpected);

  // Bail before the actual test cases, if we have no connection to service.
  ASSERT_FALSE(error_occurred_) << kConnectionErr;
  ASSERT_FALSE(timed_out) << kTimeoutErr;
  ASSERT_TRUE(received_gain_callback_);

  // received_gain_db_/received_mute_ are the current state; change if needed.
  PresetSystemGainMute();
}

// Put system into a known state (unity-gain unmuted), only changing if needed.
void SystemGainMuteTest::PresetSystemGainMute() {
  if (received_gain_db_ != kUnityGainDb) {
    SetSystemGain(kUnityGainDb);
    EXPECT_TRUE(ReceiveGainCallback(kUnityGainDb, received_mute_));
  }

  if (received_mute_) {
    SetSystemMute(false);
    EXPECT_TRUE(ReceiveGainCallback(kUnityGainDb, false));
  }
  // Once these callbacks arrive, we are primed and ready to test gain|mute.
}

// Set Gain, first resetting state so error can be detected.
void SystemGainMuteTest::SetSystemGain(float gain_db) {
  received_gain_callback_ = false;
  audio_->SetSystemGain(gain_db);
}

// Set Mute, first resetting state variable so error can be detected.
void SystemGainMuteTest::SetSystemMute(bool mute) {
  received_gain_callback_ = false;
  audio_->SetSystemMute(mute);
}

// Expecting to receive a callback, wait for it and check for errors.
bool SystemGainMuteTest::ReceiveGainCallback(float gain_db, bool mute) {
  bool timed_out = RunLoopWithTimeout(kDurationResponseExpected);
  EXPECT_FALSE(error_occurred_) << kConnectionErr;
  EXPECT_FALSE(timed_out) << kTimeoutErr;
  EXPECT_TRUE(received_gain_callback_);

  EXPECT_EQ(received_gain_db_, gain_db);
  EXPECT_EQ(received_mute_, mute);

  bool return_val = !error_occurred_ && !timed_out && received_gain_callback_ &&
                    (received_gain_db_ == gain_db) && (received_mute_ == mute);
  received_gain_callback_ = false;
  return return_val;
}

// Expecting to NOT receive a callback, wait for it and check for errors.
bool SystemGainMuteTest::ReceiveNoGainCallback() {
  bool return_val = ReceiveNoDisconnectCallback();

  EXPECT_FALSE(received_gain_callback_);
  return_val = return_val && (!received_gain_callback_);
  received_gain_callback_ = false;

  return return_val;
}

//
// Audio validation
// Tests of the asynchronous Audio interface.
//
// In some tests below, we run the message loop, so that any channel-disconnect
// from error -- with subsequent reset of the interface ptr -- can take effect.
//
// Test creation and interface independence of AudioRenderer.
TEST_F(AudioCoreTest, CreateAudioRenderer) {
  // Validate Audio can create AudioRenderer interface.
  audio_->CreateAudioRenderer(audio_renderer_.NewRequest());
  // Give time for Disconnect to occur, if it must.
  ASSERT_TRUE(ReceiveNoDisconnectCallback()) << kConnectionErr;
  EXPECT_TRUE(audio_);
  EXPECT_TRUE(audio_renderer_);

  // Validate that Audio persists without AudioRenderer.
  audio_renderer_.Unbind();
  EXPECT_FALSE(audio_renderer_);
  EXPECT_TRUE(ReceiveNoDisconnectCallback()) << kConnectionErr;
  EXPECT_TRUE(audio_);

  // Validate AudioRenderer persists after Audio is unbound.
  audio_->CreateAudioRenderer(audio_renderer_.NewRequest());
  audio_.Unbind();
  EXPECT_FALSE(audio_);
  EXPECT_TRUE(ReceiveNoDisconnectCallback()) << kConnectionErr;
  EXPECT_TRUE(audio_renderer_);
}

// Test creation and interface independence of AudioCapturer.
TEST_F(AudioCoreTest, CreateAudioCapturer) {
  // Validate Audio can create AudioCapturer interface.
  audio_->CreateAudioCapturer(audio_capturer_.NewRequest(), false);
  ASSERT_TRUE(ReceiveNoDisconnectCallback()) << kConnectionErr;
  EXPECT_TRUE(audio_) << kConnectionErr;
  EXPECT_TRUE(audio_capturer_);

  // Validate that Audio persists without AudioCapturer.
  audio_capturer_.Unbind();
  EXPECT_FALSE(audio_capturer_);
  EXPECT_TRUE(ReceiveNoDisconnectCallback()) << kConnectionErr;
  EXPECT_TRUE(audio_);

  // Validate AudioCapturer persists after Audio is unbound.
  audio_->CreateAudioCapturer(audio_capturer_.NewRequest(), true);
  audio_.Unbind();
  EXPECT_FALSE(audio_);
  EXPECT_TRUE(ReceiveNoDisconnectCallback()) << kConnectionErr;
  EXPECT_TRUE(audio_capturer_);
}

// Test setting (and re-setting) the audio output routing policy.
TEST_F(AudioCoreTest, SetRoutingPolicy) {
  audio_->SetRoutingPolicy(
      fuchsia::media::AudioOutputRoutingPolicy::ALL_PLUGGED_OUTPUTS);
  ASSERT_TRUE(ReceiveNoDisconnectCallback()) << kConnectionErr;
  EXPECT_TRUE(audio_);

  // Setting policy again should have no effect.
  audio_->SetRoutingPolicy(
      fuchsia::media::AudioOutputRoutingPolicy::ALL_PLUGGED_OUTPUTS);
  EXPECT_TRUE(ReceiveNoDisconnectCallback()) << kConnectionErr;
  EXPECT_TRUE(audio_);

  // Setting policy to different mode.
  audio_->SetRoutingPolicy(
      fuchsia::media::AudioOutputRoutingPolicy::LAST_PLUGGED_OUTPUT);
  EXPECT_TRUE(ReceiveNoDisconnectCallback()) << kConnectionErr;
  EXPECT_TRUE(audio_);
}

//
// Validation of System Gain and Mute
//
// Test setting the systemwide Mute. Initial SystemMute state is false.
TEST_F(SystemGainMuteTest, SetSystemMute_Basic) {
  SetSystemMute(true);
  EXPECT_TRUE(ReceiveGainCallback(kUnityGainDb, true));

  SetSystemMute(false);
  EXPECT_TRUE(ReceiveGainCallback(kUnityGainDb, false));
}

// Test setting the systemwide Gain. Initial SystemGain state is unity.
TEST_F(SystemGainMuteTest, SetSystemGain_Basic) {
  constexpr float expected_gain_db = kUnityGainDb - 13.5f;

  SetSystemGain(expected_gain_db);
  EXPECT_TRUE(ReceiveGainCallback(expected_gain_db, false));

  SetSystemGain(kUnityGainDb);
  EXPECT_TRUE(ReceiveGainCallback(kUnityGainDb, false));
}

// Test independence of systemwide Gain and Mute. Systemwide Mute should not
// affect systemwide Gain (should not become MUTED_GAIN_DB when Mute is true).
TEST_F(SystemGainMuteTest, SystemMuteDoesntAffectSystemGain) {
  constexpr float expected_gain_db = kUnityGainDb - 0.75f;

  SetSystemGain(expected_gain_db);
  EXPECT_TRUE(ReceiveGainCallback(expected_gain_db, false));

  SetSystemMute(true);
  EXPECT_TRUE(ReceiveGainCallback(expected_gain_db, true));

  SetSystemGain(kUnityGainDb);
  EXPECT_TRUE(ReceiveGainCallback(kUnityGainDb, true));

  SetSystemGain(expected_gain_db);
  EXPECT_TRUE(ReceiveGainCallback(expected_gain_db, true));

  SetSystemMute(false);
  EXPECT_TRUE(ReceiveGainCallback(expected_gain_db, false));

  SetSystemMute(true);
  EXPECT_TRUE(ReceiveGainCallback(expected_gain_db, true));
}

// Test independence of systemwide Gain/Mute. System Gain should not affect
// systemwide Mute (Mute should not become true when Gain is MUTED_GAIN_DB).
TEST_F(SystemGainMuteTest, SystemGainDoesntAffectSystemMute) {
  SetSystemGain(fuchsia::media::MUTED_GAIN_DB);
  EXPECT_TRUE(ReceiveGainCallback(fuchsia::media::MUTED_GAIN_DB, false));

  SetSystemMute(true);
  EXPECT_TRUE(ReceiveGainCallback(fuchsia::media::MUTED_GAIN_DB, true));

  SetSystemMute(false);
  EXPECT_TRUE(ReceiveGainCallback(fuchsia::media::MUTED_GAIN_DB, false));

  SetSystemMute(true);
  EXPECT_TRUE(ReceiveGainCallback(fuchsia::media::MUTED_GAIN_DB, true));

  constexpr float expected_gain_db = -42.0f;
  SetSystemGain(expected_gain_db);
  EXPECT_TRUE(ReceiveGainCallback(expected_gain_db, true));
}

// Test setting the systemwide Mute to the already-set value.
// In these cases, we should receive no gain|mute callback (should timeout).
// Verify this with permutations that include Mute=true and Gain=MUTED_GAIN_DB.
// 'No callback if no change in Mute' should be the case REGARDLESS of Gain.
// This test relies upon Gain-Mute independence verified by previous test.
TEST_F(SystemGainMuteTest, SystemMuteNoChangeEmitsNoCallback) {
  SetSystemMute(true);
  EXPECT_TRUE(ReceiveGainCallback(kUnityGainDb, true));

  // Expect: timeout (no callback); no change to Mute, regardless of Gain. If we
  // got a callback, either way it's an error: disconnect, or System Mute event.
  SetSystemMute(true);
  EXPECT_TRUE(ReceiveNoGainCallback());

  SetSystemGain(fuchsia::media::MUTED_GAIN_DB);
  EXPECT_TRUE(ReceiveGainCallback(fuchsia::media::MUTED_GAIN_DB, true));

  SetSystemMute(true);
  EXPECT_TRUE(ReceiveNoGainCallback());

  SetSystemMute(false);
  EXPECT_TRUE(ReceiveGainCallback(fuchsia::media::MUTED_GAIN_DB, false));

  SetSystemMute(false);
  EXPECT_TRUE(ReceiveNoGainCallback());

  SetSystemGain(kUnityGainDb);
  EXPECT_TRUE(ReceiveGainCallback(kUnityGainDb, false));

  SetSystemMute(false);
  EXPECT_TRUE(ReceiveNoGainCallback());
}

// Test setting the systemwide Gain to the already-set value.
// In these cases, we should receive no gain|mute callback (should timeout).
// Verify this with permutations that include Mute=true and Gain=MUTED_GAIN_DB.
// 'No callback if no change in Gain' should be the case REGARDLESS of Mute.
// This test relies upon Gain-Mute independence verified by previous test.
TEST_F(SystemGainMuteTest, SystemGainNoChangeEmitsNoCallback) {
  SetSystemGain(kUnityGainDb);
  EXPECT_TRUE(ReceiveNoGainCallback());

  SetSystemMute(true);
  EXPECT_TRUE(ReceiveGainCallback(kUnityGainDb, true));

  SetSystemGain(kUnityGainDb);
  EXPECT_TRUE(ReceiveNoGainCallback());

  SetSystemGain(fuchsia::media::MUTED_GAIN_DB);
  EXPECT_TRUE(ReceiveGainCallback(fuchsia::media::MUTED_GAIN_DB, true));

  SetSystemGain(fuchsia::media::MUTED_GAIN_DB);
  EXPECT_TRUE(ReceiveNoGainCallback());

  SetSystemMute(false);
  EXPECT_TRUE(ReceiveGainCallback(fuchsia::media::MUTED_GAIN_DB, false));

  SetSystemGain(fuchsia::media::MUTED_GAIN_DB);
  EXPECT_TRUE(ReceiveNoGainCallback());
}

// Set System Gain above allowed range. Should clamp to unity (which was
// previous set during SetUp); thus, no new callback should be received.
TEST_F(SystemGainMuteTest, SystemGainTooHighIsClampedToMaximum) {
  // Initial state of system gain is unity, which is the maximum value.
  SetSystemGain(kTooHighGainDb);
  EXPECT_TRUE(ReceiveNoGainCallback());

  SetSystemMute(true);
  EXPECT_TRUE(ReceiveGainCallback(kUnityGainDb, true));
}

// Set System Gain below allowed range. Should clamp "up" to the minimum value
// (which we set immediately prior); thus, no new callback should be received.
TEST_F(SystemGainMuteTest, SystemGainTooLowIsClampedToMinimum) {
  // Set system gain to the minimum value.
  SetSystemGain(fuchsia::media::MUTED_GAIN_DB);
  EXPECT_TRUE(ReceiveGainCallback(fuchsia::media::MUTED_GAIN_DB, false));

  SetSystemGain(kTooLowGainDb);
  EXPECT_TRUE(ReceiveNoGainCallback());

  SetSystemMute(true);
  EXPECT_TRUE(ReceiveGainCallback(fuchsia::media::MUTED_GAIN_DB, true));
}

}  // namespace test
}  // namespace audio
}  // namespace media
