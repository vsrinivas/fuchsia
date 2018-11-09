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
// AudioCoreTest
//
class AudioCoreTest : public gtest::RealLoopFixture {
 protected:
  void SetUp() override {
    ::gtest::RealLoopFixture::SetUp();

    environment_services_ = component::GetEnvironmentServices();
    environment_services_->ConnectToService(audio_.NewRequest());
    ASSERT_TRUE(audio_);

    audio_.set_error_handler([this](zx_status_t status) {
      error_occurred_ = true;
      QuitLoop();
    });
  }

  void TearDown() override {
    EXPECT_FALSE(error_occurred_);

    ::gtest::RealLoopFixture::TearDown();
  }

  std::shared_ptr<component::Services> environment_services_;

  fuchsia::media::AudioPtr audio_;
  fuchsia::media::AudioRendererPtr audio_renderer_;
  fuchsia::media::AudioCapturerPtr audio_capturer_;

  bool error_occurred_ = false;
};

//
// SystemGainMuteTest
//
class SystemGainMuteTest : public AudioCoreTest {
 protected:
  void SetUp() override;
  void TearDown() override;

  void SetSystemGain(float gain_db);
  void SetSystemMute(bool mute);
  bool ReceiveGainCallback(float gain_db, bool mute);
  bool ReceiveNoGainCallback();

  static constexpr float kUnityGainDb = 0.0f;

  float prev_system_gain_db_;
  bool prev_system_mute_;

  float received_gain_db_;
  bool received_mute_;

  bool received_initial_callback_ = false;
  bool received_gain_callback_ = false;
};

void SystemGainMuteTest::SetUp() {
  // Cache the previous systemwide settings for Gain and Mute, and put the
  // system into a known state as the baseline for gain&mute tests.
  AudioCoreTest::SetUp();

  audio_.events().SystemGainMuteChanged = [this](float gain_db, bool muted) {
    received_gain_db_ = gain_db;
    received_mute_ = muted;
    received_gain_callback_ = true;
    QuitLoop();
  };

  // When a client connects to Audio, the system enqueues an action to send the
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

  // Bail early if we have no connection to service.
  received_initial_callback_ = received_gain_callback_ && !timed_out;
  ASSERT_TRUE(received_initial_callback_) << kConnectionErr;

  prev_system_gain_db_ = received_gain_db_;
  prev_system_mute_ = received_mute_;

  // Now place system into a known state: unity-gain and unmuted.
  if (prev_system_gain_db_ != kUnityGainDb) {
    SetSystemGain(kUnityGainDb);
    EXPECT_TRUE(ReceiveGainCallback(kUnityGainDb, prev_system_mute_));
  }

  if (prev_system_mute_) {
    SetSystemMute(false);
    EXPECT_TRUE(ReceiveGainCallback(kUnityGainDb, false));
  }
  // Once these callbacks arrive, we are primed and ready to test gain|mute.
}

// Test is done; restore the previously-saved systemwide Gain|Mute settings.
// Also, reset the audio output routing policy (as some tests change this). This
// is split into a separate method, rather than included in TearDown(), because
// it is not needed for tests that do not change Gain|Mute or routing.
void SystemGainMuteTest::TearDown() {
  if (received_initial_callback_) {
    ASSERT_FALSE(error_occurred_);
  }

  // If we set SystemGain to the same value it currently is, we won't receive a
  // GainChange callback (and may mistake this for a deadman error). So only
  // restore System Gain if current value differs from our original saved value.
  if (received_gain_db_ != prev_system_gain_db_) {
    // Put that gain back where it came from....
    SetSystemGain(prev_system_gain_db_);
    EXPECT_TRUE(ReceiveGainCallback(prev_system_gain_db_, received_mute_));
  }

  // Same for System Mute: only change it if current value is incorrect.
  if (received_mute_ != prev_system_mute_) {
    SetSystemMute(prev_system_mute_);
    EXPECT_TRUE(ReceiveGainCallback(prev_system_gain_db_, prev_system_mute_));
  }

  // Leave this persistent systemwide setting in the default state!
  audio_->SetRoutingPolicy(
      fuchsia::media::AudioOutputRoutingPolicy::LAST_PLUGGED_OUTPUT);

  AudioCoreTest::TearDown();
}

// Set Gain, first resetting state variables so every change can be detected.
void SystemGainMuteTest::SetSystemGain(float gain_db) {
  received_gain_callback_ = false;
  audio_->SetSystemGain(gain_db);
}

// Set Mute, first resetting state variables so every change can be detected.
void SystemGainMuteTest::SetSystemMute(bool mute) {
  received_gain_callback_ = false;
  audio_->SetSystemMute(mute);
}

bool SystemGainMuteTest::ReceiveGainCallback(float gain_db, bool mute) {
  bool timeout = RunLoopWithTimeout(kDurationResponseExpected);
  EXPECT_TRUE(received_gain_callback_);
  EXPECT_EQ(received_gain_db_, gain_db);
  EXPECT_EQ(received_mute_, mute);

  bool return_val = !timeout && received_gain_callback_ &&
                    (received_gain_db_ == gain_db) && (received_mute_ == mute);
  received_gain_callback_ = false;
  return return_val;
}

bool SystemGainMuteTest::ReceiveNoGainCallback() {
  bool timeout = RunLoopWithTimeout(kDurationTimeoutExpected);
  EXPECT_FALSE(error_occurred_) << kConnectionErr;
  EXPECT_FALSE(received_gain_callback_)
      << "Unexpected SystemGainMuteChange callback received";

  bool return_val = timeout && !error_occurred_ && !received_gain_callback_;
  received_gain_callback_ = false;
  return return_val;
}

//
// TODO(mpuryear): AudioCoreTest_Negative class and tests, for cases where we
// expect Audio binding to disconnect -- and Audio interface ptr to be reset.
//

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
  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));
  ASSERT_TRUE(audio_) << kConnectionErr;
  EXPECT_TRUE(audio_renderer_);

  // Validate that Audio persists without AudioRenderer.
  audio_renderer_.Unbind();
  EXPECT_FALSE(audio_renderer_);
  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected)) << kConnectionErr;
  EXPECT_TRUE(audio_);

  // Validate AudioRenderer persists after Audio is unbound.
  audio_->CreateAudioRenderer(audio_renderer_.NewRequest());
  audio_.Unbind();
  EXPECT_FALSE(audio_);
  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected)) << kConnectionErr;
  EXPECT_TRUE(audio_renderer_);
}

// Test creation and interface independence of AudioCapturer.
TEST_F(AudioCoreTest, CreateAudioCapturer) {
  // Validate Audio can create AudioCapturer interface.
  audio_->CreateAudioCapturer(audio_capturer_.NewRequest(), false);
  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));
  ASSERT_TRUE(audio_) << kConnectionErr;
  EXPECT_TRUE(audio_capturer_);

  // Validate that Audio persists without AudioCapturer.
  audio_capturer_.Unbind();
  EXPECT_FALSE(audio_capturer_);
  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected)) << kConnectionErr;
  EXPECT_TRUE(audio_);

  // Validate AudioCapturer persists after Audio is unbound.
  audio_->CreateAudioCapturer(audio_capturer_.NewRequest(), true);
  audio_.Unbind();
  EXPECT_FALSE(audio_);
  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected)) << kConnectionErr;
  EXPECT_TRUE(audio_capturer_);
}

// Test setting the systemwide Mute.
TEST_F(SystemGainMuteTest, SetSystemMute_Basic) {
  SetSystemMute(true);
  EXPECT_TRUE(ReceiveGainCallback(kUnityGainDb, true));

  SetSystemMute(false);
  EXPECT_TRUE(ReceiveGainCallback(kUnityGainDb, false));
}

// Test setting the systemwide Gain.
TEST_F(SystemGainMuteTest, SetSystemGain_Basic) {
  constexpr float expected_gain_db = -11.0f;

  SetSystemGain(expected_gain_db);
  EXPECT_TRUE(ReceiveGainCallback(expected_gain_db, false));

  SetSystemMute(true);
  EXPECT_TRUE(ReceiveGainCallback(expected_gain_db, true));

  SetSystemGain(kUnityGainDb);
  EXPECT_TRUE(ReceiveGainCallback(kUnityGainDb, true));
}

// Test the independence of systemwide Gain and Mute. Setting the system Gain to
// -- and away from -- MUTED_GAIN_DB should have no effect on the system Mute.
TEST_F(SystemGainMuteTest, SetSystemMute_Independence) {
  SetSystemGain(fuchsia::media::MUTED_GAIN_DB);
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
TEST_F(SystemGainMuteTest, SetSystemMute_NoCallbackIfNoChange) {
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
TEST_F(SystemGainMuteTest, SetSystemGain_NoCallbackIfNoChange) {
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

// Test setting (and re-setting) the audio output routing policy.
TEST_F(AudioCoreTest, SetRoutingPolicy) {
  audio_->SetRoutingPolicy(
      fuchsia::media::AudioOutputRoutingPolicy::ALL_PLUGGED_OUTPUTS);
  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected)) << kConnectionErr;

  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));
  ASSERT_TRUE(audio_) << kConnectionErr;

  // Setting policy again should have no effect.
  audio_->SetRoutingPolicy(
      fuchsia::media::AudioOutputRoutingPolicy::ALL_PLUGGED_OUTPUTS);
  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected)) << kConnectionErr;

  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));
  ASSERT_TRUE(audio_) << kConnectionErr;

  // Setting policy to different mode.
  audio_->SetRoutingPolicy(
      fuchsia::media::AudioOutputRoutingPolicy::LAST_PLUGGED_OUTPUT);
  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected)) << kConnectionErr;

  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));
  ASSERT_TRUE(audio_) << kConnectionErr;
}

}  // namespace test
}  // namespace audio
}  // namespace media
