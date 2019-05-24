// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <lib/gtest/real_loop_fixture.h>

#include <cmath>

#include "lib/component/cpp/environment_services_helper.h"
#include "src/media/audio/audio_core/test/audio_tests_shared.h"

namespace media::audio::test {

//
// AudioBase
//
class AudioBase : public gtest::RealLoopFixture {
 protected:
  void SetUp() override;
  void TearDown() override;

  bool ReceiveNoDisconnectCallback();

  std::shared_ptr<component::Services> environment_services_;
  fuchsia::media::AudioPtr audio_;
  fuchsia::media::AudioRendererPtr audio_renderer_;
  fuchsia::media::AudioCapturerPtr audio_capturer_;

  bool error_occurred_ = false;
};

// TODO(mpuryear): Create tests to explicitly target the AudioCore protocol.
// One of the first focus areas should be EnableDeviceSettings()

//
// AudioTest
//
class AudioTest : public AudioBase {
 protected:
  void SetUp() override;
};

//
// SystemGainMuteTest class
//
class SystemGainMuteTest : public AudioBase {
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
// AudioBase implementation
//
// Connect to Audio interface and set an error handler
void AudioBase::SetUp() {
  gtest::RealLoopFixture::SetUp();

  environment_services_ = component::GetEnvironmentServices();
  environment_services_->ConnectToService(audio_.NewRequest());
  audio_.set_error_handler(
      [this](zx_status_t error) { error_occurred_ = true; });
}

void AudioBase::TearDown() {
  ASSERT_FALSE(error_occurred_);

  gtest::RealLoopFixture::TearDown();
}

// Expecting NOT to receive a disconnect. Wait, then check for errors.
//
// TODO(mpuryear): Refactor tests to eliminate "wait for nothing bad to happen".
bool AudioBase::ReceiveNoDisconnectCallback() {
  bool timed_out = !RunLoopWithTimeoutOrUntil(
      [this]() { return error_occurred_; }, kDurationTimeoutExpected);

  EXPECT_FALSE(error_occurred_);

  EXPECT_TRUE(timed_out) << kNoTimeoutErr;

  return !error_occurred_ && timed_out;
}

//
// AudioTest implementation
//
void AudioTest::SetUp() {
  AudioBase::SetUp();

  // TODO(mpuryear): Refactor to eliminate "wait for nothing bad to happen".
  ASSERT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected)) << kConnectionErr;
  ASSERT_TRUE(audio_.is_bound());
}

//
// SystemGainMuteTest implementation
//
// Register for notification of SystemGainMute changes; receive initial values
// and set the system to a known baseline for gain/mute testing.
void SystemGainMuteTest::SetUp() {
  AudioBase::SetUp();

  audio_.events().SystemGainMuteChanged = [this](float gain_db, bool muted) {
    received_gain_db_ = gain_db;
    received_mute_ = muted;
    received_gain_callback_ = true;
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
  // Gain|Mute settings until they are changed. Wait for this callback now.
  bool timed_out = !RunLoopWithTimeoutOrUntil(
      [this]() { return error_occurred_ || received_gain_callback_; },
      kDurationResponseExpected, kDurationGranularity);
  ASSERT_TRUE(audio_.is_bound());

  // Bail before the actual test cases, if we have no connection to service.
  ASSERT_FALSE(error_occurred_) << kConnectionErr;
  ASSERT_FALSE(timed_out) << kTimeoutErr;
  ASSERT_TRUE(received_gain_callback_);

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
  bool timed_out = !RunLoopWithTimeoutOrUntil(
      [this, &gain_db, &mute]() {
        return error_occurred_ ||
               (received_gain_db_ == gain_db && received_mute_ == mute);
      },
      kDurationResponseExpected, kDurationGranularity);

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
// The following 3 conditions are validated:
// 1. Audio can create AudioRenderer.
// 2. Audio persists after created AudioRenderer is destroyed.
// 3. AudioRenderer persists after Audio is destroyed.
// 4. Asynchronous Audio can create synchronous AudioCapturers, too.
//
// TODO(mpuryear): Refactor tests to eliminate "wait for nothing bad to happen".
TEST_F(AudioTest, CreateAudioRenderer) {
  auto err_handler = [this](zx_status_t error) { error_occurred_ = true; };

  fuchsia::media::AudioPtr audio_2;
  fuchsia::media::AudioPtr audio_3;
  fuchsia::media::AudioPtr audio_4;

  environment_services_->ConnectToService(audio_2.NewRequest());
  environment_services_->ConnectToService(audio_3.NewRequest());
  environment_services_->ConnectToService(audio_4.NewRequest());

  audio_2.set_error_handler(err_handler);
  audio_3.set_error_handler(err_handler);
  audio_4.set_error_handler(err_handler);

  fuchsia::media::AudioRendererPtr audio_renderer_2;
  fuchsia::media::AudioRendererPtr audio_renderer_3;
  fuchsia::media::AudioRendererSyncPtr audio_renderer_sync;

  audio_->CreateAudioRenderer(audio_renderer_.NewRequest());
  audio_2->CreateAudioRenderer(audio_renderer_2.NewRequest());
  audio_3->CreateAudioRenderer(audio_renderer_3.NewRequest());
  audio_4->CreateAudioRenderer(audio_renderer_sync.NewRequest());

  audio_renderer_.set_error_handler(err_handler);
  audio_renderer_2.set_error_handler(err_handler);
  audio_renderer_3.set_error_handler(err_handler);

  audio_renderer_2.Unbind();
  audio_3.Unbind();

  // ...give the two interfaces a chance to completely unbind...
  ASSERT_FALSE(RunLoopWithTimeoutOrUntil([this]() { return (error_occurred_); },
                                         kDurationTimeoutExpected * 2));

  // Validate Audio can create AudioRenderer interface.
  EXPECT_TRUE(audio_.is_bound());
  EXPECT_TRUE(audio_renderer_.is_bound());

  // Validate that Audio2 persists without AudioRenderer2.
  EXPECT_TRUE(audio_2.is_bound());
  EXPECT_FALSE(audio_renderer_2.is_bound());

  // Validate AudioRenderer3 persists after Audio3 is unbound.
  EXPECT_FALSE(audio_3.is_bound());
  EXPECT_TRUE(audio_renderer_3.is_bound());

  // Validate AudioRendererSync was successfully created.
  EXPECT_TRUE(audio_4.is_bound());
  EXPECT_TRUE(audio_renderer_sync.is_bound());
}

// Test behavior of null or bad parameters. Both cases should cleanly fail
// without causing the Audio FIDL channel to disconnect.
TEST_F(AudioTest, CreateBadAudioRenderer) {
  // Passing in a null request should have no effect.
  audio_->CreateAudioRenderer(nullptr);

  // Malformed request should not affect audio2
  auto err_handler = [this](zx_status_t error) { error_occurred_ = true; };
  fuchsia::media::AudioPtr audio_2;
  environment_services_->ConnectToService(audio_2.NewRequest());
  audio_2.set_error_handler(err_handler);

  // Corrupt the contents of this request.
  fidl::InterfaceRequest<fuchsia::media::AudioRenderer> bad_request;
  auto bad_request_void_ptr = static_cast<void*>(&bad_request);
  auto bad_request_dword_ptr = static_cast<uint32_t*>(bad_request_void_ptr);
  *bad_request_dword_ptr = 0x0BADCAFE;

  audio_->CreateAudioRenderer(std::move(bad_request));

  // Give time for Disconnect to occur, if it must.
  EXPECT_TRUE(ReceiveNoDisconnectCallback()) << kConnectionErr;

  EXPECT_TRUE(audio_.is_bound());
  EXPECT_TRUE(audio_2.is_bound());

  // TODO(mpuryear): test cases where inner contents of request are corrupt.
}

// Test creation and interface independence of AudioCapturer.
// The following 3 conditions are validated:
// 1. Audio can create AudioCapturer.
// 2. Audio persists after created AudioCapturer is destroyed.
// 3. AudioCapturer persists after Audio is destroyed.
// 4. Asynchronous Audio can create synchronous AudioCapturers, too.
//
// TODO(mpuryear): Refactor tests to eliminate "wait for nothing bad to happen".
TEST_F(AudioTest, CreateAudioCapturer) {
  auto err_handler = [this](zx_status_t error) { error_occurred_ = true; };

  fuchsia::media::AudioPtr audio_2;
  fuchsia::media::AudioPtr audio_3;
  fuchsia::media::AudioPtr audio_4;

  environment_services_->ConnectToService(audio_2.NewRequest());
  environment_services_->ConnectToService(audio_3.NewRequest());
  environment_services_->ConnectToService(audio_4.NewRequest());

  audio_2.set_error_handler(err_handler);
  audio_3.set_error_handler(err_handler);
  audio_4.set_error_handler(err_handler);

  fuchsia::media::AudioCapturerPtr audio_capturer_2;
  fuchsia::media::AudioCapturerPtr audio_capturer_3;
  fuchsia::media::AudioCapturerSyncPtr audio_capturer_sync;

  audio_->CreateAudioCapturer(audio_capturer_.NewRequest(), false);
  audio_2->CreateAudioCapturer(audio_capturer_2.NewRequest(), false);
  audio_3->CreateAudioCapturer(audio_capturer_3.NewRequest(), true);
  audio_4->CreateAudioCapturer(audio_capturer_sync.NewRequest(), false);

  audio_capturer_.set_error_handler(err_handler);
  audio_capturer_2.set_error_handler(err_handler);
  audio_capturer_3.set_error_handler(err_handler);

  audio_capturer_2.Unbind();
  audio_3.Unbind();

  // ...give the two interfaces a chance to completely unbind...
  ASSERT_FALSE(RunLoopWithTimeoutOrUntil([this]() { return (error_occurred_); },
                                         kDurationTimeoutExpected * 2));

  // Validate Audio can create AudioCapturer interfaces.
  EXPECT_TRUE(audio_.is_bound());
  EXPECT_TRUE(audio_capturer_.is_bound());

  // Validate that Audio2 persists without AudioCapturer2.
  EXPECT_TRUE(audio_2.is_bound());
  EXPECT_FALSE(audio_capturer_2.is_bound());

  // Validate AudioCapturer3 persists after Audio3 is unbound.
  EXPECT_FALSE(audio_3.is_bound());
  EXPECT_TRUE(audio_capturer_3.is_bound());

  // Validate AudioCapturerSync was successfully created.
  EXPECT_TRUE(audio_4.is_bound());
  EXPECT_TRUE(audio_capturer_sync.is_bound());
}

// Test behavior of null or bad parameters. Both cases should cleanly fail
// without causing the Audio FIDL channel to disconnect.
TEST_F(AudioTest, CreateBadAudioCapturer) {
  // Passing in a null request should have no effect.
  audio_->CreateAudioCapturer(nullptr, false);

  // Malformed request should not affect audio2
  auto err_handler = [this](zx_status_t error) { error_occurred_ = true; };
  fuchsia::media::AudioPtr audio_2;
  environment_services_->ConnectToService(audio_2.NewRequest());
  audio_2.set_error_handler(err_handler);

  // Corrupt the contents of this request.
  fidl::InterfaceRequest<fuchsia::media::AudioCapturer> bad_request;
  auto bad_request_void_ptr = static_cast<void*>(&bad_request);
  auto bad_request_dword_ptr = static_cast<uint32_t*>(bad_request_void_ptr);
  *bad_request_dword_ptr = 0x0BADCAFE;
  audio_2->CreateAudioCapturer(std::move(bad_request), true);

  // Give time for Disconnect to occur, if it must.
  EXPECT_TRUE(ReceiveNoDisconnectCallback()) << kConnectionErr;

  EXPECT_TRUE(audio_.is_bound());
  EXPECT_TRUE(audio_2.is_bound());

  // TODO(mpuryear): test cases where inner contents of request are corrupt.
}

// Test setting (and re-setting) the audio output routing policy.
TEST_F(AudioTest, SetRoutingPolicy) {
  audio_->SetRoutingPolicy(
      fuchsia::media::AudioOutputRoutingPolicy::ALL_PLUGGED_OUTPUTS);

  // Setting policy again should have no effect.
  audio_->SetRoutingPolicy(
      fuchsia::media::AudioOutputRoutingPolicy::ALL_PLUGGED_OUTPUTS);

  // Out-of-range enum should cause debug message, but no disconnect.
  audio_->SetRoutingPolicy(
      static_cast<fuchsia::media::AudioOutputRoutingPolicy>(-1u));

  // Setting policy to different mode.
  audio_->SetRoutingPolicy(
      fuchsia::media::AudioOutputRoutingPolicy::LAST_PLUGGED_OUTPUT);
  EXPECT_TRUE(ReceiveNoDisconnectCallback());
  EXPECT_TRUE(audio_.is_bound());
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
  SetSystemGain(fuchsia::media::audio::MUTED_GAIN_DB);
  EXPECT_TRUE(ReceiveGainCallback(fuchsia::media::audio::MUTED_GAIN_DB, false));

  SetSystemMute(true);
  EXPECT_TRUE(ReceiveGainCallback(fuchsia::media::audio::MUTED_GAIN_DB, true));

  SetSystemMute(false);
  EXPECT_TRUE(ReceiveGainCallback(fuchsia::media::audio::MUTED_GAIN_DB, false));

  SetSystemMute(true);
  EXPECT_TRUE(ReceiveGainCallback(fuchsia::media::audio::MUTED_GAIN_DB, true));

  constexpr float expected_gain_db = -42.0f;
  SetSystemGain(expected_gain_db);
  EXPECT_TRUE(ReceiveGainCallback(expected_gain_db, true));
}

// Test setting the systemwide Mute to the already-set value.
// In these cases, we should receive no mute callback (should timeout).
// Verify this with permutations that include Mute=true and Gain=MUTED_GAIN_DB.
// 'No callback if no change in Mute' should be the case REGARDLESS of Gain.
// This test relies upon Gain-Mute independence verified by previous test.
TEST_F(SystemGainMuteTest, SystemMuteNoChangeEmitsNoCallback) {
  SetSystemMute(true);
  EXPECT_TRUE(ReceiveGainCallback(kUnityGainDb, true));

  SetSystemMute(true);
  EXPECT_TRUE(ReceiveNoGainCallback());

  SetSystemGain(fuchsia::media::audio::MUTED_GAIN_DB);
  EXPECT_TRUE(ReceiveGainCallback(fuchsia::media::audio::MUTED_GAIN_DB, true));

  SetSystemMute(false);
  EXPECT_TRUE(ReceiveGainCallback(fuchsia::media::audio::MUTED_GAIN_DB, false));

  SetSystemMute(false);
  EXPECT_TRUE(ReceiveNoGainCallback());
}

// Test setting the systemwide Gain to the already-set value.
// In these cases, we should receive no gain callback (should timeout).
// Verify this with permutations that include Mute=true and Gain=MUTED_GAIN_DB.
// 'No callback if no change in Gain' should be the case REGARDLESS of Mute.
// This test relies upon Gain-Mute independence verified by previous test.
TEST_F(SystemGainMuteTest, SystemGainNoChangeEmitsNoCallback) {
  SetSystemMute(true);
  EXPECT_TRUE(ReceiveGainCallback(kUnityGainDb, true));

  SetSystemGain(kUnityGainDb);
  EXPECT_TRUE(ReceiveNoGainCallback());

  SetSystemGain(fuchsia::media::audio::MUTED_GAIN_DB);
  EXPECT_TRUE(ReceiveGainCallback(fuchsia::media::audio::MUTED_GAIN_DB, true));

  SetSystemMute(false);
  EXPECT_TRUE(ReceiveGainCallback(fuchsia::media::audio::MUTED_GAIN_DB, false));

  SetSystemGain(fuchsia::media::audio::MUTED_GAIN_DB);
  EXPECT_TRUE(ReceiveNoGainCallback());
}

// Set System Gain above allowed range, after setting to low value.
// Initial state of system gain is unity, which is the maximum value.
TEST_F(SystemGainMuteTest, SystemGainTooHighIsClampedToMaximum) {
  SetSystemGain(fuchsia::media::audio::MUTED_GAIN_DB);
  EXPECT_TRUE(ReceiveGainCallback(fuchsia::media::audio::MUTED_GAIN_DB, false));

  SetSystemGain(kTooHighGainDb);
  EXPECT_TRUE(ReceiveGainCallback(kUnityGainDb, false));
}

// Set System Gain below allowed range. Should clamp "up" to the minimum val.
TEST_F(SystemGainMuteTest, SystemGainTooLowIsClampedToMinimum) {
  SetSystemGain(kTooLowGainDb);
  EXPECT_TRUE(ReceiveGainCallback(fuchsia::media::audio::MUTED_GAIN_DB, false));
}

// Set System Gain to malformed float. Should cause no change, nor disconnect.
TEST_F(SystemGainMuteTest, SystemGainNanHasNoEffect) {
  SetSystemGain(NAN);
  EXPECT_TRUE(ReceiveNoGainCallback());
}

}  // namespace media::audio::test
