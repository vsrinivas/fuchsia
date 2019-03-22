// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/test/gain_control_test.h"

#include <lib/gtest/real_loop_fixture.h>
#include <cmath>

#include "garnet/bin/media/audio_core/test/audio_tests_shared.h"

namespace media::audio::test {

// GainControlTestBase
//
void GainControlTestBase::SetUp() {
  ::gtest::RealLoopFixture::SetUp();

  auto err_handler = [this](zx_status_t error) {
    error_occurred_ = true;
    QuitLoop();
  };

  environment_services_ = component::GetEnvironmentServices();
  environment_services_->ConnectToService(audio_.NewRequest());
  audio_.set_error_handler(err_handler);
}

void GainControlTestBase::TearDown() {
  // Base Audio interface should still survive even when the others are reset.
  ASSERT_TRUE(AudioIsBound());

  // These expect_ vars indicate negative cases where we expect failure.
  EXPECT_EQ(ApiIsNull(), expect_null_api_);

  EXPECT_EQ(error_occurred_, expect_error_);
  EXPECT_EQ(!gain_control_.is_bound(), expect_null_gain_control_);

  EXPECT_EQ(error_occurred_2_, expect_error_2_);
  EXPECT_EQ(!gain_control_2_.is_bound(), expect_null_gain_control_2_);
}

void GainControlTestBase::SetUpRenderer() {
  auto err_handler = [this](zx_status_t error) {
    error_occurred_ = true;
    QuitLoop();
  };

  audio_->CreateAudioRenderer(audio_renderer_.NewRequest());
  audio_renderer_.set_error_handler(err_handler);
}

void GainControlTestBase::SetUpCapturer() {
  auto err_handler = [this](zx_status_t error) {
    error_occurred_ = true;
    QuitLoop();
  };

  audio_->CreateAudioCapturer(audio_capturer_.NewRequest(), false);
  audio_capturer_.set_error_handler(err_handler);
}

void GainControlTestBase::SetUpRenderer2() {
  auto err_handler = [this](zx_status_t error) {
    error_occurred_2_ = true;
    QuitLoop();
  };

  audio_->CreateAudioRenderer(audio_renderer_2_.NewRequest());
  audio_renderer_2_.set_error_handler(err_handler);
}

void GainControlTestBase::SetUpCapturer2() {
  auto err_handler = [this](zx_status_t error) {
    error_occurred_2_ = true;
    QuitLoop();
  };

  audio_->CreateAudioCapturer(audio_capturer_2_.NewRequest(), false);
  audio_capturer_2_.set_error_handler(err_handler);
}

void GainControlTestBase::SetUpGainControl() {
  auto err_handler = [this](zx_status_t error) {
    error_occurred_ = true;
    QuitLoop();
  };
  gain_control_.set_error_handler(err_handler);

  gain_control_.events().OnGainMuteChanged = [this](float gain_db, bool muted) {
    received_gain_callback_ = true;
    received_gain_db_ = gain_db;
    received_mute_ = muted;
    QuitLoop();
  };

  expect_null_gain_control_ = false;
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
  auto err_handler = [this](zx_status_t error) {
    error_occurred_2_ = true;
    QuitLoop();
  };
  gain_control_2_.set_error_handler(err_handler);

  gain_control_2_.events().OnGainMuteChanged = [this](float gain_db,
                                                      bool muted) {
    received_gain_callback_2_ = true;

    received_gain_db_2_ = gain_db;
    received_mute_2_ = muted;
    QuitLoop();
  };

  expect_null_gain_control_2_ = false;
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
  expect_null_api_ = true;
  expect_error_ = true;
  expect_null_gain_control_ = true;
}

// Set Gain, asserting that state is already reset so error can be detected.
void GainControlTestBase::SetGain(float gain_db) {
  gain_control_->SetGain(gain_db);
}

// Set Mute, asserting that state is already reset so error can be detected.
void GainControlTestBase::SetMute(bool mute) { gain_control_->SetMute(mute); }

// Tests expect a gain callback. Absorb this; perform related error checking.
bool GainControlTestBase::ReceiveGainCallback(float gain_db, bool mute) {
  received_gain_callback_ = false;

  bool timed_out = !RunLoopWithTimeoutOrUntil(
      [this, gain_db, mute]() {
        return (error_occurred_ ||
                ((received_gain_db_ == gain_db) && (received_mute_ == mute)));
      },
      kDurationResponseExpected, kDurationGranularity);

  EXPECT_FALSE(error_occurred_) << kConnectionErr;
  EXPECT_TRUE(gain_control_.is_bound());

  EXPECT_FALSE(timed_out) << kTimeoutErr;

  EXPECT_TRUE(received_gain_callback_);
  EXPECT_EQ(received_gain_db_, gain_db);
  EXPECT_EQ(received_mute_, mute);

  return (!error_occurred_ && !timed_out);
}

// Tests expect to receive neither gain callback nor error; assert this.
//
// TODO(mpuryear): Refactor tests to eliminate "wait for nothing bad to happen".
bool GainControlTestBase::ReceiveNoGainCallback() {
  received_gain_callback_ = false;

  bool timed_out = !RunLoopWithTimeoutOrUntil(
      [this]() { return (error_occurred_ || received_gain_callback_); },
      kDurationTimeoutExpected);

  EXPECT_FALSE(error_occurred_) << kConnectionErr;
  EXPECT_TRUE(gain_control_.is_bound());

  EXPECT_TRUE(timed_out) << kNoTimeoutErr;

  EXPECT_FALSE(received_gain_callback_);

  return (!error_occurred_ && !received_gain_callback_);
}

// Tests expect to receive a disconnect callback for API binding, then for
// GainControl binding. Treat any regular gain callback received as error.
bool GainControlTestBase::ReceiveDisconnectCallback() {
  received_gain_callback_ = false;

  bool timed_out = !RunLoopWithTimeoutOrUntil(
      [this]() {
        return (ApiIsNull() && !gain_control_.is_bound()) ||
               received_gain_callback_;
      },
      kDurationResponseExpected, kDurationGranularity);

  // If GainControl causes disconnect, renderer/capturer disconnects first.
  EXPECT_TRUE(error_occurred_);
  EXPECT_TRUE(ApiIsNull());
  EXPECT_FALSE(gain_control_.is_bound());

  EXPECT_FALSE(timed_out);

  EXPECT_FALSE(received_gain_callback_);

  return (!timed_out && !received_gain_callback_);
}

// Test implementations, called by various objects across the class hierarchy
void GainControlTestBase::TestSetGain() {
  constexpr float expect_gain_db = 20.0f;
  SetGain(expect_gain_db);
  EXPECT_TRUE(ReceiveGainCallback(expect_gain_db, false));

  SetGain(kUnityGainDb);
  EXPECT_TRUE(ReceiveGainCallback(kUnityGainDb, false));
}

void GainControlTestBase::TestSetMute() {
  float expect_mute = true;
  SetMute(expect_mute);
  EXPECT_TRUE(ReceiveGainCallback(kUnityGainDb, expect_mute));

  expect_mute = false;
  SetMute(expect_mute);
  EXPECT_TRUE(ReceiveGainCallback(kUnityGainDb, expect_mute));
}

void GainControlTestBase::TestSetGainMute() {
  constexpr float expect_gain_db = -5.5f;
  constexpr bool expect_mute = true;

  SetGain(expect_gain_db);
  SetMute(expect_mute);

  EXPECT_TRUE(ReceiveGainCallback(expect_gain_db, false));
  EXPECT_TRUE(ReceiveGainCallback(expect_gain_db, expect_mute));
}

void GainControlTestBase::TestDuplicateSetGain() {
  constexpr float expect_gain_db = 20.0f;
  SetGain(expect_gain_db);
  EXPECT_TRUE(ReceiveGainCallback(expect_gain_db, false));

  SetGain(expect_gain_db);
  EXPECT_TRUE(ReceiveNoGainCallback());
}

void GainControlTestBase::TestDuplicateSetMute() {
  float expect_mute = true;
  SetMute(expect_mute);
  EXPECT_TRUE(ReceiveGainCallback(kUnityGainDb, expect_mute));

  SetMute(expect_mute);
  EXPECT_TRUE(ReceiveNoGainCallback());
}

// For negative expectations.
//
// Setting gain too high should cause a disconnect.
void GainControlTestBase::TestSetGainTooHigh() {
  SetNegativeExpectations();

  constexpr float expect_gain_db = kTooHighGainDb;
  SetGain(expect_gain_db);

  EXPECT_TRUE(ReceiveDisconnectCallback()) << "Bindings did not disconnect!";
  EXPECT_FALSE(gain_control_.is_bound());
}

// Setting gain too low should cause a disconnect.
void GainControlTestBase::TestSetGainTooLow() {
  SetNegativeExpectations();

  constexpr float expect_gain_db = kTooLowGainDb;
  SetGain(expect_gain_db);

  EXPECT_TRUE(ReceiveDisconnectCallback()) << "Bindings did not disconnect!";
  EXPECT_FALSE(gain_control_.is_bound());
}

// Setting stream-specific gain to NAN should cause both FIDL channels
// (renderer/capturer and gain_control) to disconnect.
void GainControlTestBase::TestSetGainNaN() {
  SetNegativeExpectations();

  constexpr float expect_gain_db = NAN;
  SetGain(expect_gain_db);

  EXPECT_TRUE(ReceiveDisconnectCallback()) << "Bindings did not disconnect!";
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

// N.B. DuplicateSetGain behavior is tested in RendererTwoGainControlsTest.
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

  expect_null_gain_control_2_ = true;
  expect_error_2_ = true;
}

// Tests expect a gain callback on both gain_controls, with the provided gain_db
// and mute values -- and no errors.
bool SiblingGainControlsTest::ReceiveGainCallback(float gain_db, bool mute) {
  received_gain_callback_ = received_gain_callback_2_ = false;
  received_gain_db_ = received_gain_db_2_ = kTooLowGainDb;

  bool timed_out = !RunLoopWithTimeoutOrUntil(
      [this, gain_db, mute]() {
        return (error_occurred_ || error_occurred_2_ ||
                ((received_gain_db_ == gain_db) && (received_mute_ == mute) &&
                 (received_gain_db_2_ == gain_db) &&
                 (received_mute_2_ == mute)));
      },
      kDurationResponseExpected, kDurationGranularity);

  EXPECT_FALSE(error_occurred_);
  EXPECT_FALSE(error_occurred_2_);
  EXPECT_FALSE(ApiIsNull());
  EXPECT_TRUE(gain_control_.is_bound());
  EXPECT_TRUE(gain_control_2_.is_bound());

  EXPECT_FALSE(timed_out);

  EXPECT_TRUE(received_gain_callback_);
  EXPECT_TRUE(received_gain_callback_2_);
  EXPECT_EQ(received_gain_db_, gain_db);
  EXPECT_EQ(received_gain_db_2_, gain_db);
  EXPECT_EQ(received_mute_, mute);
  EXPECT_EQ(received_mute_2_, mute);

  return (!timed_out && !error_occurred_ && !error_occurred_2_);
}

// Tests expect neither gain interface to receive gain callback nor error.
//
// TODO(mpuryear): Refactor tests to eliminate "wait for nothing bad to happen".
bool SiblingGainControlsTest::ReceiveNoGainCallback() {
  received_gain_callback_ = received_gain_callback_2_ = false;

  bool timed_out = !RunLoopWithTimeoutOrUntil(
      [this]() {
        return (error_occurred_ || error_occurred_2_ ||
                received_gain_callback_ || received_gain_callback_2_);
      },
      kDurationTimeoutExpected);

  EXPECT_FALSE(error_occurred_);
  EXPECT_FALSE(error_occurred_2_);
  EXPECT_FALSE(ApiIsNull());
  EXPECT_TRUE(gain_control_.is_bound());
  EXPECT_TRUE(gain_control_2_.is_bound());

  EXPECT_TRUE(timed_out) << kNoTimeoutErr;

  EXPECT_FALSE(received_gain_callback_);
  EXPECT_FALSE(received_gain_callback_2_);

  return (!error_occurred_ && !error_occurred_2_ && !received_gain_callback_ &&
          !received_gain_callback_2_);
}

// Tests expect to receive a disconnect callback for the API binding, then
// one for each of the two GainControl bindings. In our loop, we wait until all
// three of these have occurred. Also, if any normal gain callback is received
// during this time, it is unexpected and treated as an error.
bool SiblingGainControlsTest::ReceiveDisconnectCallback() {
  received_gain_callback_ = received_gain_callback_2_ = false;

  bool timed_out = !RunLoopWithTimeoutOrUntil(
      [this]() {
        return (ApiIsNull()) && (!gain_control_.is_bound()) &&
               (!gain_control_2_.is_bound());
      },
      kDurationResponseExpected, kDurationGranularity);

  EXPECT_TRUE(error_occurred_);
  EXPECT_TRUE(error_occurred_2_);
  EXPECT_TRUE(ApiIsNull());
  EXPECT_FALSE(gain_control_.is_bound());
  EXPECT_FALSE(gain_control_2_.is_bound());

  EXPECT_FALSE(timed_out);

  EXPECT_FALSE(received_gain_callback_);
  EXPECT_FALSE(received_gain_callback_2_);

  return (!timed_out && !received_gain_callback_ && !received_gain_callback_2_);
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
// API binding and gain_control (thus we wait for timeout below).
//
// TODO(mpuryear): Refactor tests to eliminate "wait for nothing bad to happen".
bool IndependentGainControlsTest::ReceiveGainCallback(float gain_db,
                                                      bool mute) {
  received_gain_callback_ = received_gain_callback_2_ = false;
  received_gain_db_ = kTooLowGainDb;

  bool timed_out = !RunLoopWithTimeoutOrUntil(
      [this]() {
        return (error_occurred_ || error_occurred_2_ ||
                received_gain_callback_2_);
      },
      kDurationTimeoutExpected);

  EXPECT_FALSE(error_occurred_);
  EXPECT_FALSE(error_occurred_2_);
  EXPECT_FALSE(ApiIsNull());
  EXPECT_TRUE(gain_control_.is_bound());
  EXPECT_TRUE(gain_control_2_.is_bound());

  EXPECT_TRUE(timed_out);

  EXPECT_TRUE(received_gain_callback_);
  EXPECT_FALSE(received_gain_callback_2_);
  EXPECT_EQ(received_gain_db_, gain_db);
  EXPECT_EQ(received_mute_, mute);

  // Not only must we not have disconnected or received unexpected gain2
  // callback, also gain1 must have received the expected callback.
  return (!error_occurred_ && !error_occurred_2_ &&
          !received_gain_callback_2_ && received_gain_db_ == gain_db &&
          received_mute_ == mute);
}

// Tests expect to receive neither gain callback nor error, on both gains.
//
// TODO(mpuryear): Refactor tests to eliminate "wait for nothing bad to happen".
bool IndependentGainControlsTest::ReceiveNoGainCallback() {
  received_gain_callback_ = received_gain_callback_2_ = false;

  bool timed_out = !RunLoopWithTimeoutOrUntil(
      [this]() {
        return (error_occurred_ || error_occurred_2_ ||
                received_gain_callback_ || received_gain_callback_2_);
      },
      kDurationTimeoutExpected);

  EXPECT_FALSE(error_occurred_);
  EXPECT_FALSE(error_occurred_2_);
  EXPECT_FALSE(ApiIsNull());
  EXPECT_TRUE(gain_control_.is_bound());
  EXPECT_TRUE(gain_control_2_.is_bound());

  EXPECT_TRUE(timed_out);

  EXPECT_FALSE(received_gain_callback_);
  EXPECT_FALSE(received_gain_callback_2_);

  return (!error_occurred_ && !error_occurred_2_ && !received_gain_callback_ &&
          !received_gain_callback_2_);
}

// Tests expect to receive a disconnect callback for the API binding, then
// another for the GainControl binding. If before unbinding, that GainControl
// generates a gain callback, this is unexpected and treated as an error. We
// still expect nothing from the independent API binding and its gain_control
// (thus we wait for timeout).
//
// TODO(mpuryear): Refactor tests to eliminate "wait for nothing bad to happen".
bool IndependentGainControlsTest::ReceiveDisconnectCallback() {
  received_gain_callback_ = received_gain_callback_2_ = false;

  bool timed_out = !RunLoopWithTimeoutOrUntil(
      [this]() {
        return error_occurred_2_ || received_gain_callback_ ||
               received_gain_callback_2_;
      },
      kDurationTimeoutExpected);

  EXPECT_TRUE(error_occurred_);
  EXPECT_FALSE(error_occurred_2_);
  EXPECT_TRUE(ApiIsNull());
  EXPECT_FALSE(gain_control_.is_bound());
  EXPECT_TRUE(gain_control_2_.is_bound());

  EXPECT_TRUE(timed_out);

  EXPECT_FALSE(received_gain_callback_);
  EXPECT_FALSE(received_gain_callback_2_);

  // While waiting for (but not receiving) gain2 disconnect or either gain
  // callback, we should also have received the gain1 disconnect.
  return (error_occurred_ && !error_occurred_2_ && !received_gain_callback_ &&
          !received_gain_callback_2_);
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
