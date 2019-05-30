// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>

#include <cmath>

#include "src/media/audio/lib/test/audio_core_test_base.h"

namespace media::audio::test {

//
// AudioTest
//
class AudioTest : public AudioCoreTestBase {
 protected:
  void TearDown() override;

  fuchsia::media::AudioRendererPtr audio_renderer_;
  fuchsia::media::AudioCapturerPtr audio_capturer_;
};

//
// SystemGainMuteTest class
//
class SystemGainMuteTest : public AudioCoreTestBase {
 protected:
  void SetUp() override;
  void TearDown() override;

  void PresetSystemGainMute();
  void SetSystemGain(float gain_db);
  void SetSystemMute(bool mute);
  void ExpectGainCallback(float gain_db, bool mute);

  float received_gain_db_;
  bool received_mute_;
};

//
// AudioTest implementation
//
void AudioTest::TearDown() {
  if (audio_core_.is_bound()) {
    audio_core_.events().SystemGainMuteChanged = nullptr;
  }

  audio_renderer_.Unbind();
  audio_capturer_.Unbind();

  AudioCoreTestBase::TearDown();
}

//
// SystemGainMuteTest implementation
//
// Register for notification of SystemGainMute changes; receive initial values
// and set the system to a known baseline for gain/mute testing.
void SystemGainMuteTest::SetUp() {
  AudioCoreTestBase::SetUp();

  audio_core_.events().SystemGainMuteChanged =
      CompletionCallback([this](float gain_db, bool muted) {
        received_gain_db_ = gain_db;
        received_mute_ = muted;
      });

  // When clients connects to Audio, the system enqueues an action to send the
  // newly-connected client a callback with the systemwide Gain|Mute settings.
  // The system executes this action after the client's currently executing task
  // completes. This means that if a client establishes a connection and then
  // registers a SystemGainMuteChanged callback BEFORE returning, this client
  // will subsequently (once the system gets a chance to run) receive an initial
  // notification of Gain|Mute settings at the time of connection. Conversely,
  // if a client DOES return before registering, even after subsequently
  // registering for the event the client has no way of learning the current
  // Gain|Mute settings until they are changed. Wait for this callback now.
  ExpectCallback();
  ASSERT_TRUE(audio_core_.is_bound());

  // Bail before the actual test cases, if we have no connection to service.
  ASSERT_FALSE(error_occurred_) << kDisconnectErr;

  PresetSystemGainMute();
}

void SystemGainMuteTest::TearDown() {
  if (audio_core_.is_bound()) {
    audio_core_.events().SystemGainMuteChanged = nullptr;
  }

  AudioCoreTestBase::TearDown();
}

// Put system into a known state (unity-gain unmuted), only changing if needed.
void SystemGainMuteTest::PresetSystemGainMute() {
  if (received_gain_db_ != kUnityGainDb) {
    SetSystemGain(kUnityGainDb);
    ExpectGainCallback(kUnityGainDb, received_mute_);
  }

  if (received_mute_) {
    SetSystemMute(false);
    ExpectGainCallback(kUnityGainDb, false);
  }

  // Once these callbacks arrive, we are primed and ready to test gain|mute.
}

// Set Gain, first resetting state so error can be detected.
void SystemGainMuteTest::SetSystemGain(float gain_db) {
  audio_core_->SetSystemGain(gain_db);
}

// Set Mute, first resetting state variable so error can be detected.
void SystemGainMuteTest::SetSystemMute(bool mute) {
  audio_core_->SetSystemMute(mute);
}

// Expecting to receive a callback, wait for it and check for errors.
void SystemGainMuteTest::ExpectGainCallback(float gain_db, bool mute) {
  received_gain_db_ = kTooLowGainDb;

  ExpectCallback();
  EXPECT_EQ(received_gain_db_, gain_db);
  EXPECT_EQ(received_mute_, mute);
}

//
// Audio validation
// Tests of the asynchronous Audio interface.
//
// In some tests below, we run the message loop, so that any channel-disconnect
// from error -- with subsequent reset of the interface ptr -- can take effect.
//
// Test creation and interface independence of AudioRenderer.
// The following 4 conditions are validated:
// 1. Audio can create AudioRenderer.
// 2. Audio persists after created AudioRenderer is destroyed.
// 3. AudioRenderer2 persists after Audio2 is destroyed.
// 4. Asynchronous Audio can create synchronous AudioRenderer, too.
TEST_F(AudioTest, CreateAudioRenderer) {
  audio_core_->CreateAudioRenderer(audio_renderer_.NewRequest());
  audio_renderer_.set_error_handler(ErrorHandler());

  fuchsia::media::AudioRendererSyncPtr audio_renderer_sync;
  audio_core_->CreateAudioRenderer(audio_renderer_sync.NewRequest());

  fuchsia::media::AudioCorePtr audio_core_2;
  startup_context_->svc()->Connect(audio_core_2.NewRequest());
  audio_core_2.set_error_handler(ErrorHandler());

  fuchsia::media::AudioRendererPtr audio_renderer_2;
  audio_core_2->CreateAudioRenderer(audio_renderer_2.NewRequest());
  audio_renderer_2.set_error_handler(ErrorHandler());

  // Before unbinding these, verify they survived this far.
  EXPECT_TRUE(audio_core_2.is_bound());
  audio_core_2.Unbind();

  EXPECT_TRUE(audio_renderer_.is_bound());
  audio_renderer_.Unbind();

  // ...allow them to completely unbind. Will it affect their parent/child?
  audio_renderer_2->GetMinLeadTime(CompletionCallback([](int64_t) {}));
  ExpectCallback();

  // Validate AudioRendererSync was successfully created.
  EXPECT_TRUE(audio_renderer_sync.is_bound());

  // Validate child AudioRenderer2 persists after parent Audio2 was unbound.
  EXPECT_TRUE(audio_renderer_2.is_bound());

  // TearDown will validate that parent Audio survived after child unbound.
}

// Test creation and interface independence of AudioCapturer.
// The following 4 conditions are validated:
// 1. Audio can create AudioCapturer.
// 2. Audio persists after created AudioCapturer is destroyed.
// 3. AudioCapturer2 persists after Audio2 is destroyed.
// 4. Asynchronous Audio can create synchronous AudioCapturer, too.
TEST_F(AudioTest, CreateAudioCapturer) {
  audio_core_->CreateAudioCapturer(false, audio_capturer_.NewRequest());
  audio_capturer_.set_error_handler(ErrorHandler());

  fuchsia::media::AudioCapturerSyncPtr audio_capturer_sync;
  audio_core_->CreateAudioCapturer(false, audio_capturer_sync.NewRequest());

  fuchsia::media::AudioCorePtr audio_core_2;
  startup_context_->svc()->Connect(audio_core_2.NewRequest());
  audio_core_2.set_error_handler(ErrorHandler());

  fuchsia::media::AudioCapturerPtr audio_capturer_2;
  audio_core_2->CreateAudioCapturer(false, audio_capturer_2.NewRequest());
  audio_capturer_2.set_error_handler(ErrorHandler());

  // Before unbinding these, verify they survived this far.
  EXPECT_TRUE(audio_core_2.is_bound());
  audio_core_2.Unbind();

  EXPECT_TRUE(audio_capturer_.is_bound());
  audio_capturer_.Unbind();

  // ...allow them to completely unbind. Will it affect their parent/child?
  audio_capturer_2->GetStreamType(
      CompletionCallback([](fuchsia::media::StreamType) {}));
  ExpectCallback();

  // Validate AudioCapturerSync was successfully created.
  EXPECT_TRUE(audio_capturer_sync.is_bound());

  // Validate AudioCapturer2 persists after Audio2 was unbound.
  EXPECT_TRUE(audio_capturer_2.is_bound());

  // TearDown will validate that parent Audio survived after child unbound.
}

//
// TODO(mpuryear): "fuzz" tests (FIDL-compliant but protocol-inconsistent).
//

// Test setting (and re-setting) the audio output routing policy.
TEST_F(AudioTest, SetRoutingPolicy) {
  audio_core_->SetRoutingPolicy(
      fuchsia::media::AudioOutputRoutingPolicy::ALL_PLUGGED_OUTPUTS);

  // Setting policy again should have no effect.
  audio_core_->SetRoutingPolicy(
      fuchsia::media::AudioOutputRoutingPolicy::ALL_PLUGGED_OUTPUTS);

  // Setting policy to different mode.
  audio_core_->SetRoutingPolicy(
      fuchsia::media::AudioOutputRoutingPolicy::LAST_PLUGGED_OUTPUT);

  // Give time for Disconnect to occur if it must, but we expect this callback.
  audio_core_.events().SystemGainMuteChanged =
      CompletionCallback([](float, bool) {});

  audio_core_->SetSystemGain(-1.0f);
  audio_core_->SetSystemGain(0.0f);
  ExpectCallback();
}

// Out-of-range enum should cause debug message, but no disconnect.
TEST_F(AudioTest, SetBadRoutingPolicy) {
  audio_core_->SetRoutingPolicy(
      static_cast<fuchsia::media::AudioOutputRoutingPolicy>(-1u));

  // Give time for Disconnect to occur if it must, but we expect this callback.
  audio_core_.events().SystemGainMuteChanged =
      CompletionCallback([](float gain_db, bool muted) {});

  audio_core_->SetSystemGain(-1.0f);
  audio_core_->SetSystemGain(0.0f);
  ExpectCallback();
}

//
// Validation of System Gain and Mute
//
// Test setting the systemwide Mute. Initial SystemMute state is false.
TEST_F(SystemGainMuteTest, SetSystemMute_Basic) {
  SetSystemMute(true);
  ExpectGainCallback(kUnityGainDb, true);

  SetSystemMute(false);
  ExpectGainCallback(kUnityGainDb, false);
}

// Test setting the systemwide Gain. Initial SystemGain state is unity.
TEST_F(SystemGainMuteTest, SetSystemGain_Basic) {
  constexpr float expected_gain_db = kUnityGainDb - 13.5f;

  SetSystemGain(expected_gain_db);
  ExpectGainCallback(expected_gain_db, false);

  SetSystemGain(kUnityGainDb);
  ExpectGainCallback(kUnityGainDb, false);
}

// Test independence of systemwide Gain and Mute. Systemwide Mute should not
// affect systemwide Gain (should not become MUTED_GAIN_DB when Mute is true).
TEST_F(SystemGainMuteTest, SystemMuteDoesntAffectSystemGain) {
  constexpr float expected_gain_db = kUnityGainDb - 0.75f;

  SetSystemGain(expected_gain_db);
  ExpectGainCallback(expected_gain_db, false);

  SetSystemMute(true);
  ExpectGainCallback(expected_gain_db, true);

  SetSystemGain(kUnityGainDb);
  ExpectGainCallback(kUnityGainDb, true);

  SetSystemGain(expected_gain_db);
  ExpectGainCallback(expected_gain_db, true);

  SetSystemMute(false);
  ExpectGainCallback(expected_gain_db, false);

  SetSystemMute(true);
  ExpectGainCallback(expected_gain_db, true);
}

// Test independence of systemwide Gain/Mute. System Gain should not affect
// systemwide Mute (Mute should not become true when Gain is MUTED_GAIN_DB).
TEST_F(SystemGainMuteTest, SystemGainDoesntAffectSystemMute) {
  SetSystemGain(fuchsia::media::audio::MUTED_GAIN_DB);
  ExpectGainCallback(fuchsia::media::audio::MUTED_GAIN_DB, false);

  SetSystemMute(true);
  ExpectGainCallback(fuchsia::media::audio::MUTED_GAIN_DB, true);

  SetSystemMute(false);
  ExpectGainCallback(fuchsia::media::audio::MUTED_GAIN_DB, false);

  SetSystemMute(true);
  ExpectGainCallback(fuchsia::media::audio::MUTED_GAIN_DB, true);

  constexpr float expected_gain_db = -42.0f;
  SetSystemGain(expected_gain_db);
  ExpectGainCallback(expected_gain_db, true);
}

// Validate cases when we should receive no OnSystemGainMuteChanged callback:
// 1) Setting systemwide Mute to the already-set value;
// 2) Setting systemwide Gain to the already-set value;
// 3) Setting systemwide Gain to a malformed float value.
// Then immediate change system Mute: we should receive only the final callback.
TEST_F(SystemGainMuteTest, SystemGainMuteNoCallback) {
  SetSystemGain(kUnityGainDb);
  SetSystemMute(false);
  SetSystemGain(NAN);

  SetSystemMute(true);

  ExpectGainCallback(kUnityGainDb, true);
}

// Set System Gain above allowed range, after setting to low value.
// Initial state of system gain is unity, which is the maximum value.
TEST_F(SystemGainMuteTest, SystemGainTooHighIsClampedToMaximum) {
  SetSystemGain(fuchsia::media::audio::MUTED_GAIN_DB);
  ExpectGainCallback(fuchsia::media::audio::MUTED_GAIN_DB, false);

  SetSystemGain(kTooHighGainDb);
  ExpectGainCallback(kUnityGainDb, false);
}

// Set System Gain below allowed range. Should clamp "up" to the minimum val.
TEST_F(SystemGainMuteTest, SystemGainTooLowIsClampedToMinimum) {
  SetSystemGain(kTooLowGainDb);
  ExpectGainCallback(fuchsia::media::audio::MUTED_GAIN_DB, false);
}

}  // namespace media::audio::test
