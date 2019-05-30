// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/test/gain_control_test.h"

#include <cmath>

#include "gtest/gtest.h"
#include "src/media/audio/lib/test/audio_test_base.h"

namespace media::audio::test {

// GainControlTestBase
//
void GainControlTestBase::TearDown() {
  EXPECT_EQ(!gain_control_.is_bound(), null_gain_control_expected_);
  gain_control_.Unbind();

  EXPECT_EQ(error_occurred_2_, error_expected_2_);
  EXPECT_EQ(!gain_control_2_.is_bound(), null_gain_control_expected_2_);
  gain_control_2_.Unbind();

  // These expect_ vars indicate negative cases where we expect failure.
  EXPECT_EQ(ApiIsNull(), null_api_expected_);
  audio_renderer_.Unbind();
  audio_capturer_.Unbind();
  audio_renderer_2_.Unbind();
  audio_capturer_2_.Unbind();

  AudioCoreTestBase::TearDown();
}

void GainControlTestBase::SetUpRenderer() {
  audio_core_->CreateAudioRenderer(audio_renderer_.NewRequest());
  audio_renderer_.set_error_handler(ErrorHandler());
}

void GainControlTestBase::SetUpCapturer() {
  audio_core_->CreateAudioCapturer(false, audio_capturer_.NewRequest());
  audio_capturer_.set_error_handler(ErrorHandler());
}

void GainControlTestBase::SetUpRenderer2() {
  audio_core_->CreateAudioRenderer(audio_renderer_2_.NewRequest());
  audio_renderer_2_.set_error_handler(
      ErrorHandler([this](zx_status_t) { error_occurred_2_ = true; }));
}

void GainControlTestBase::SetUpCapturer2() {
  audio_core_->CreateAudioCapturer(false, audio_capturer_2_.NewRequest());
  audio_capturer_2_.set_error_handler(
      ErrorHandler([this](zx_status_t) { error_occurred_2_ = true; }));
}

void GainControlTestBase::SetUpGainControl() {
  gain_control_.set_error_handler(ErrorHandler());

  gain_control_.events().OnGainMuteChanged =
      CompletionCallback([this](float gain_db, bool muted) {
        received_gain_db_ = gain_db;
        received_mute_ = muted;
      });

  null_gain_control_expected_ = false;
}

void GainControlTestBase::SetUpGainControlOnRenderer() {
  audio_renderer_->BindGainControl(gain_control_.NewRequest());
  SetUpGainControl();
}

void GainControlTestBase::SetUpGainControlOnCapturer() {
  audio_capturer_->BindGainControl(gain_control_.NewRequest());
  SetUpGainControl();
}

void GainControlTestBase::SetUpGainControl2() {
  gain_control_2_.set_error_handler(
      ErrorHandler([this](zx_status_t) { error_occurred_2_ = true; }));

  gain_control_2_.events().OnGainMuteChanged =
      CompletionCallback([this](float gain_db, bool muted) {
        received_gain_db_2_ = gain_db;
        received_mute_2_ = muted;
      });

  null_gain_control_expected_2_ = false;
}

void GainControlTestBase::SetUpGainControl2OnRenderer() {
  audio_renderer_->BindGainControl(gain_control_2_.NewRequest());
  SetUpGainControl2();
}

void GainControlTestBase::SetUpGainControl2OnCapturer() {
  audio_capturer_->BindGainControl(gain_control_2_.NewRequest());
  SetUpGainControl2();
}

void GainControlTestBase::SetUpGainControl2OnRenderer2() {
  audio_renderer_2_->BindGainControl(gain_control_2_.NewRequest());
  SetUpGainControl2();
}

void GainControlTestBase::SetUpGainControl2OnCapturer2() {
  audio_capturer_2_->BindGainControl(gain_control_2_.NewRequest());
  SetUpGainControl2();
}

// For tests that cause a GainControl to disconnect, set these expectations.
void GainControlTestBase::SetNegativeExpectations() {
  AudioCoreTestBase::SetNegativeExpectations();

  null_api_expected_ = true;
  null_gain_control_expected_ = true;
}

// Set Gain, asserting that state is already reset so error can be detected.
void GainControlTestBase::SetGain(float gain_db) {
  gain_control_->SetGain(gain_db);
}

// Set Mute, asserting that state is already reset so error can be detected.
void GainControlTestBase::SetMute(bool mute) { gain_control_->SetMute(mute); }

// Expect and absorb a single gain callback; perform related error checking.
void GainControlTestBase::ExpectGainCallback(float gain_db, bool mute) {
  received_gain_db_ = kTooLowGainDb;

  ExpectCondition([this, &gain_db, &mute]() {
    return (received_gain_db_ == gain_db) && (received_mute_ == mute);
  });
  EXPECT_EQ(received_gain_db_, gain_db);
  EXPECT_EQ(received_mute_, mute);
  EXPECT_FALSE(error_occurred_);
}

// Tests expect to receive a disconnect callback for API binding, then for
// GainControl binding. Treat any regular gain callback received as error.
void GainControlTestBase::ExpectDisconnect() {
  // Need to wait for both renderer/capturer AND gain_control to disconnect.
  AudioCoreTestBase::ExpectDisconnect();

  if (gain_control_.is_bound() || !ApiIsNull()) {
    // Reset our error detector before listening again.
    error_occurred_ = false;
    AudioCoreTestBase::ExpectDisconnect();
  }

  EXPECT_TRUE(ApiIsNull());
  EXPECT_FALSE(gain_control_.is_bound());
}

// Test implementations, called by various objects across the class hierarchy
void GainControlTestBase::TestSetGain() {
  constexpr float expect_gain_db = 20.0f;

  SetGain(expect_gain_db);
  ExpectGainCallback(expect_gain_db, false);

  SetGain(kUnityGainDb);
  ExpectGainCallback(kUnityGainDb, false);
}

void GainControlTestBase::TestSetMute() {
  bool expect_mute = true;

  SetMute(expect_mute);
  ExpectGainCallback(kUnityGainDb, expect_mute);

  expect_mute = false;
  SetMute(expect_mute);
  ExpectGainCallback(kUnityGainDb, expect_mute);
}

void GainControlTestBase::TestSetGainMute() {
  constexpr float expect_gain_db = -5.5f;

  SetGain(expect_gain_db);
  SetMute(true);

  ExpectGainCallback(expect_gain_db, true);
}

void GainControlTestBase::TestDuplicateSetGain() {
  constexpr float expect_gain_db = 20.0f;

  SetGain(expect_gain_db);
  ExpectGainCallback(expect_gain_db, false);

  SetGain(expect_gain_db);
  SetMute(true);
  // Rather than waiting for "no gain callback", we set an (independent) mute
  // value and expect only a single callback that includes the more recent mute.
  ExpectGainCallback(expect_gain_db, true);
}

void GainControlTestBase::TestDuplicateSetMute() {
  constexpr float expect_gain_db = -42.0f;

  SetMute(true);
  ExpectGainCallback(kUnityGainDb, true);

  SetMute(true);
  SetGain(expect_gain_db);
  // Rather than waiting for "no mute callback", we set an (independent) gain
  // value and expect only a single callback that includes the more recent gain.
  ExpectGainCallback(expect_gain_db, true);
}

// For negative expectations.
//
// Setting gain too high should cause a disconnect.
void GainControlTestBase::TestSetGainTooHigh() {
  SetNegativeExpectations();

  constexpr float expect_gain_db = kTooHighGainDb;
  SetGain(expect_gain_db);

  ExpectDisconnect();
  EXPECT_FALSE(gain_control_.is_bound());
}

// Setting gain too low should cause a disconnect.
void GainControlTestBase::TestSetGainTooLow() {
  SetNegativeExpectations();

  constexpr float expect_gain_db = kTooLowGainDb;
  SetGain(expect_gain_db);

  ExpectDisconnect();
  EXPECT_FALSE(gain_control_.is_bound());
}

// Setting stream-specific gain to NAN should cause both FIDL channels
// (renderer/capturer and gain_control) to disconnect.
void GainControlTestBase::TestSetGainNaN() {
  SetNegativeExpectations();

  constexpr float expect_gain_db = NAN;
  SetGain(expect_gain_db);

  ExpectDisconnect();
  EXPECT_FALSE(gain_control_.is_bound());
}

//
// Basic GainControl validation with single instance.
//

// RenderGainControlTest
//
void RenderGainControlTest::SetUp() {
  GainControlTestBase::SetUp();

  SetUpRenderer();
  SetUpGainControlOnRenderer();
}

// Single renderer with one gain control: Gain, Mute and GainMute combo.
//
TEST_F(RenderGainControlTest, SetGain) { TestSetGain(); }

TEST_F(RenderGainControlTest, SetMute) { TestSetMute(); }

TEST_F(RenderGainControlTest, SetGainMute) { TestSetGainMute(); }

// TODO(mpuryear): Ramp-related tests (render). Relevant FIDL signature is:
//   SetGainWithRamp(float32 gain_db, int64 duration_ns, RampType ramp_type);

// TODO(mpuryear): Validate GainChange notifications of gainramps.

TEST_F(RenderGainControlTest, DuplicateSetGain) { TestDuplicateSetGain(); }

TEST_F(RenderGainControlTest, DuplicateSetMute) { TestDuplicateSetMute(); }

TEST_F(RenderGainControlTest, SetGainTooHigh) { TestSetGainTooHigh(); }

TEST_F(RenderGainControlTest, SetGainTooLow) { TestSetGainTooLow(); }

TEST_F(RenderGainControlTest, SetGainNaN) { TestSetGainNaN(); }

// TODO(mpuryear): Ramp-related negative tests, across all scenarios

// CaptureGainControlTest
//
void CaptureGainControlTest::SetUp() {
  GainControlTestBase::SetUp();

  SetUpCapturer();
  SetUpGainControlOnCapturer();
}

// Single capturer with one gain control
//
TEST_F(CaptureGainControlTest, SetGain) { TestSetGain(); }

TEST_F(CaptureGainControlTest, SetMute) { TestSetMute(); }

TEST_F(CaptureGainControlTest, SetGainMute) { TestSetGainMute(); }

// TODO(mpuryear): Ramp-related tests (capture)

TEST_F(CaptureGainControlTest, DuplicateSetGain) { TestDuplicateSetGain(); }
// N.B. DuplicateSetMute behavior is tested in CapturerTwoGainControlsTest.

TEST_F(CaptureGainControlTest, SetGainTooHigh) { TestSetGainTooHigh(); }

TEST_F(CaptureGainControlTest, SetGainTooLow) { TestSetGainTooLow(); }

TEST_F(CaptureGainControlTest, SetGainNaN) { TestSetGainNaN(); }

// SiblingGainControlsTest
// On a renderer/capturer, sibling GainControls receive identical notifications.
//
// For tests that cause a GainControl to disconnect, set these expectations.
void SiblingGainControlsTest::SetNegativeExpectations() {
  GainControlTestBase::SetNegativeExpectations();

  null_gain_control_expected_2_ = true;
  error_expected_2_ = true;
}

// Tests expect a gain callback on both gain_controls, with the provided gain_db
// and mute values -- and no errors.
void SiblingGainControlsTest::ExpectGainCallback(float gain_db, bool mute) {
  received_gain_db_ = kTooLowGainDb;
  received_gain_db_2_ = kTooLowGainDb;

  ExpectCondition([this, gain_db, mute]() {
    return error_occurred_ ||
           (received_gain_db_ == gain_db && received_gain_db_2_ == gain_db &&
            received_mute_ == mute && received_mute_2_ == mute);
  });

  EXPECT_FALSE(error_occurred_) << kDisconnectErr;
  EXPECT_FALSE(ApiIsNull());
  EXPECT_TRUE(gain_control_.is_bound());
  EXPECT_TRUE(gain_control_2_.is_bound());

  EXPECT_EQ(received_gain_db_, gain_db);
  EXPECT_EQ(received_gain_db_2_, gain_db);
  EXPECT_EQ(received_mute_, mute);
  EXPECT_EQ(received_mute_2_, mute);
}

// Tests expect to receive a disconnect callback for the API binding, then
// one for each of the two GainControl bindings. In our loop, we wait until all
// three of these have occurred. Also, if any normal gain callback is received
// during this time, it is unexpected and treated as an error.
void SiblingGainControlsTest::ExpectDisconnect() {
  SetNegativeExpectations();
  received_gain_db_2_ = kTooLowGainDb;

  // Wait Renderer/Capturer and BOTH GainControls to disconnect. Because
  // multiple disconnect callbacks could arrive between our polling interval, we
  // wait a maximum of three times, checking between them for completion.
  AudioCoreTestBase::ExpectDisconnect();
  if (!ApiIsNull() || gain_control_.is_bound() || gain_control_2_.is_bound()) {
    // Reset our error detector before listening again.
    error_occurred_ = false;
    AudioCoreTestBase::ExpectDisconnect();
  }
  if (!ApiIsNull() || gain_control_.is_bound() || gain_control_2_.is_bound()) {
    // Reset our error detector before listening again.
    error_occurred_ = false;
    AudioCoreTestBase::ExpectDisconnect();
  }

  EXPECT_TRUE(error_occurred_2_);
  EXPECT_EQ(received_gain_db_2_, kTooLowGainDb);
}

// RendererTwoGainControlsTest
// Renderer with two gain controls: both should receive identical notifications.
//
void RendererTwoGainControlsTest::SetUp() {
  SiblingGainControlsTest::SetUp();

  SetUpRenderer();
  SetUpGainControl2OnRenderer();
  SetUpGainControlOnRenderer();
}

TEST_F(RendererTwoGainControlsTest, BothControlsReceiveGainNotifications) {
  TestSetGain();
}

TEST_F(RendererTwoGainControlsTest, BothControlsReceiveMuteNotifications) {
  TestSetMute();
}

TEST_F(RendererTwoGainControlsTest, DuplicateSetGain) {
  TestDuplicateSetGain();
}

// N.B. DuplicateSetMute behavior is tested in RendererGainControlTest.

TEST_F(RendererTwoGainControlsTest, SetGainTooHigh) { TestSetGainTooHigh(); }

TEST_F(RendererTwoGainControlsTest, SetGainTooLow) { TestSetGainTooLow(); }

TEST_F(RendererTwoGainControlsTest, SetGainNaN) { TestSetGainNaN(); }

// CapturerTwoGainControlsTest
// Capturer with two gain controls: both should receive identical notifications.
//
void CapturerTwoGainControlsTest::SetUp() {
  SiblingGainControlsTest::SetUp();

  SetUpCapturer();
  SetUpGainControl2OnCapturer();
  SetUpGainControlOnCapturer();
}

TEST_F(CapturerTwoGainControlsTest, BothControlsReceiveGainNotifications) {
  TestSetGain();
}

TEST_F(CapturerTwoGainControlsTest, BothControlsReceiveMuteNotifications) {
  TestSetMute();
}

// N.B. DuplicateSetGain behavior is tested in CapturerGainControlTest.
TEST_F(CapturerTwoGainControlsTest, DuplicateSetMute) {
  TestDuplicateSetMute();
}

TEST_F(CapturerTwoGainControlsTest, SetGainTooHigh) { TestSetGainTooHigh(); }

TEST_F(CapturerTwoGainControlsTest, SetGainTooLow) { TestSetGainTooLow(); }

TEST_F(CapturerTwoGainControlsTest, SetGainNaN) { TestSetGainNaN(); }

// IndependentGainControlsTest
// Verify that GainControls on different API instances are fully independent.
//

// Tests expect a gain callback and no error, and neither on the independent
// API binding and gain_control (thus we check for subsequent callback below).
void IndependentGainControlsTest::ExpectGainCallback(float gain_db, bool mute) {
  received_gain_db_2_ = kTooLowGainDb;

  GainControlTestBase::ExpectGainCallback(gain_db, mute);

  // Not only must we not have disconnected or received unexpected gain2
  // callback, also gain1 must have received the expected callback.
  EXPECT_EQ(received_gain_db_2_, kTooLowGainDb);

  // Even if we did get the gain callback we wanted, now we check for other
  // gain callbacks -- or a disconnect. If any of these occur, then we fail.
  if (!error_occurred_ && received_gain_db_ == gain_db &&
      received_gain_db_2_ == kTooLowGainDb) {
    received_gain_db_ = kTooLowGainDb;

    RunLoopUntilIdle();

    EXPECT_FALSE(error_occurred_) << kDisconnectErr;
    EXPECT_EQ(received_gain_db_, kTooLowGainDb);
    EXPECT_EQ(received_gain_db_2_, kTooLowGainDb);
  }
}

// Tests expect to receive a disconnect callback for the API binding, then
// another for the GainControl binding. If before unbinding, that GainControl
// generates a gain callback, this is unexpected and treated as an error. We
// still expect nothing from the independent API binding and its gain_control
// (thus we wait for timeout).
void IndependentGainControlsTest::ExpectDisconnect() {
  received_gain_db_2_ = kTooLowGainDb;

  // We expect Renderer/Capturer AND GainControl to disconnect. Wait for both.
  // We do NOT expect second renderer/capturer to disconnect nor other callback.
  GainControlTestBase::ExpectDisconnect();

  // Even if we did get the disconnect callbacks we wanted, now wait for other
  // unexpected callbacks. If none occur, then we pass.
  RunLoopUntilIdle();

  // After these disconnects, both Gain and API should be gone, but not Gain2.
  EXPECT_FALSE(error_occurred_2_) << "Unexpected disconnect: independent gain";
  EXPECT_TRUE(gain_control_2_.is_bound());

  EXPECT_EQ(received_gain_db_2_, kTooLowGainDb);
}

// TwoRenderersGainControlsTest
// Two renderers, each with a gain control: we expect no cross-impact.
//
void TwoRenderersGainControlsTest::SetUp() {
  IndependentGainControlsTest::SetUp();

  SetUpRenderer2();
  SetUpGainControl2OnRenderer2();

  SetUpRenderer();
  SetUpGainControlOnRenderer();
}

TEST_F(TwoRenderersGainControlsTest, OtherInstanceReceivesNoMuteNotification) {
  TestSetMute();
}

// We expect primary GainControl/Renderer to disconnect.
TEST_F(TwoRenderersGainControlsTest, SetGainTooLow) { TestSetGainTooLow(); }

// RendererCapturerGainControlsTest
// Renderer gain control should not affect capturer gain control.
//
void RendererCapturerGainControlsTest::SetUp() {
  IndependentGainControlsTest::SetUp();

  SetUpCapturer();
  SetUpGainControl2OnCapturer();

  SetUpRenderer();
  SetUpGainControlOnRenderer();
}

TEST_F(RendererCapturerGainControlsTest,
       OtherInstanceReceivesNoGainNotification) {
  TestSetGain();
}

// We expect primary GainControl/Renderer to disconnect.
TEST_F(RendererCapturerGainControlsTest, SetGainTooHigh) {
  TestSetGainTooHigh();
}

// CapturerRendererGainControlsTest
// Capturer gain control should not affect renderer gain control.
//
void CapturerRendererGainControlsTest::SetUp() {
  IndependentGainControlsTest::SetUp();

  SetUpRenderer();
  SetUpGainControl2OnRenderer();

  SetUpCapturer();
  SetUpGainControlOnCapturer();
}

TEST_F(CapturerRendererGainControlsTest,
       OtherInstanceReceivesNoGainNotification) {
  TestSetGain();
}

// We expect primary GainControl/Capturer to disconnect.
TEST_F(CapturerRendererGainControlsTest, SetGainTooHigh) {
  TestSetGainTooHigh();
}

// TwoCapturersGainControlsTest
// Two capturers, each with a gain control: we expect no cross-impact.
//
void TwoCapturersGainControlsTest::SetUp() {
  IndependentGainControlsTest::SetUp();

  SetUpCapturer2();
  SetUpGainControl2OnCapturer2();

  SetUpCapturer();
  SetUpGainControlOnCapturer();
}

TEST_F(TwoCapturersGainControlsTest, OtherInstanceReceivesNoMuteNotification) {
  TestSetMute();
}

// We expect primary GainControl/Capturer to disconnect.
TEST_F(TwoCapturersGainControlsTest, SetGainTooLow) { TestSetGainTooLow(); }

}  // namespace media::audio::test
