// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/test/gain_control_test.h"

#include <fuchsia/media/cpp/fidl.h>
#include <lib/gtest/real_loop_fixture.h>

#include "garnet/bin/media/audio_core/test/audio_fidl_tests_shared.h"
#include "lib/component/cpp/environment_services_helper.h"
#include "lib/fxl/logging.h"

namespace media {
namespace audio {
namespace test {

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

  ASSERT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));
  ASSERT_TRUE(AudioIsBound());
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

  ASSERT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));
  ASSERT_TRUE(audio_renderer_.is_bound());
}

void GainControlTestBase::SetUpCapturer() {
  auto err_handler = [this](zx_status_t error) {
    error_occurred_ = true;
    QuitLoop();
  };

  audio_->CreateAudioCapturer(audio_capturer_.NewRequest(), false);
  audio_capturer_.set_error_handler(err_handler);

  ASSERT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));
  ASSERT_TRUE(audio_capturer_.is_bound());
}

void GainControlTestBase::SetUpRenderer2() {
  auto err_handler = [this](zx_status_t error) {
    error_occurred_2_ = true;
    QuitLoop();
  };

  audio_->CreateAudioRenderer(audio_renderer_2_.NewRequest());
  audio_renderer_2_.set_error_handler(err_handler);

  ASSERT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));
  ASSERT_TRUE(audio_renderer_2_.is_bound());
}

void GainControlTestBase::SetUpCapturer2() {
  auto err_handler = [this](zx_status_t error) {
    error_occurred_2_ = true;
    QuitLoop();
  };

  audio_->CreateAudioCapturer(audio_capturer_2_.NewRequest(), false);
  audio_capturer_2_.set_error_handler(err_handler);

  ASSERT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));
  ASSERT_TRUE(audio_capturer_2_.is_bound());
}

void GainControlTestBase::SetUpGainControl() {
  auto err_handler = [this](zx_status_t error) {
    error_occurred_ = true;
    QuitLoop();
  };
  gain_control_.set_error_handler(err_handler);

  ASSERT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));
  ASSERT_TRUE(gain_control_.is_bound());

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

  ASSERT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));
  ASSERT_TRUE(gain_control_2_.is_bound());

  gain_control_2_.events().OnGainMuteChanged = [this](float gain_db,
                                                      bool muted) {
    received_gain_callback_2_ = true;

    received_gain_db_2_ = gain_db;
    received_mute_2_ = muted;
    QuitLoop();
  };

  expect_null_gain_control_2_ = false;

  // Give interfaces a chance to disconnect if they must.
  EXPECT_TRUE(ReceiveNoGainCallback());
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

// Set Gain, asserting that state is already reset so error can be detected.
void GainControlTestBase::SetGain(float gain_db) {
  // On initialization and every Receive...Callback(), this is set to false.
  ASSERT_FALSE(received_gain_callback_)
      << "Failed to reset received_gain_callback_ previously";

  gain_control_->SetGain(gain_db);
}

// Set Mute, asserting that state is already reset so error can be detected.
void GainControlTestBase::SetMute(bool mute) {
  // On initialization and every Receive...Callback(), this is set to false.
  ASSERT_FALSE(received_gain_callback_)
      << "Failed to reset received_gain_callback_ previously";

  gain_control_->SetMute(mute);
}

// Tests expect a gain callback. Absorb this; perform related error checking.
bool GainControlTestBase::ReceiveGainCallback(float gain_db, bool mute) {
  bool timed_out = RunLoopWithTimeout(kDurationResponseExpected);
  EXPECT_TRUE(gain_control_.is_bound());

  // For this check, one and only one of these 3 should always be true.
  EXPECT_TRUE(received_gain_callback_);
  EXPECT_FALSE(error_occurred_) << kConnectionErr;
  EXPECT_FALSE(timed_out) << kTimeoutErr;

  EXPECT_EQ(received_gain_db_, gain_db);
  EXPECT_EQ(received_mute_, mute);

  bool return_val = received_gain_callback_ && !error_occurred_ && !timed_out;
  return_val &= (received_gain_db_ == gain_db) && (received_mute_ == mute);

  received_gain_callback_ = false;
  return return_val;
}

// Tests expect to receive neither gain callback nor error; assert this.
bool GainControlTestBase::ReceiveNoGainCallback() {
  bool timed_out = RunLoopWithTimeout(kDurationTimeoutExpected);
  EXPECT_TRUE(gain_control_.is_bound());

  // For this check, one and only one of these 3 should always be true.
  EXPECT_TRUE(timed_out) << kNoTimeoutErr;
  EXPECT_FALSE(error_occurred_) << kConnectionErr;
  EXPECT_FALSE(received_gain_callback_);

  bool return_val = timed_out && !error_occurred_ && !received_gain_callback_;

  received_gain_callback_ = false;
  return return_val;
}

// Tests expect to receive a disconnect callback for the API binding, then
// another for the GainControl binding. Parent base class asserts that after
// the first disconnect, the GainControl is still bound.
bool GainControlTestBase::ReceiveDisconnectCallback() {
  bool timed_out = RunLoopWithTimeout(kDurationTimeoutExpected);

  // For this check, one and only one of these 3 should always be true.
  EXPECT_TRUE(error_occurred_);
  EXPECT_FALSE(timed_out);
  EXPECT_FALSE(received_gain_callback_);

  // If GainControl causes disconnect, renderer/capturer disconnects first.
  bool api_is_null = ApiIsNull();
  EXPECT_TRUE(api_is_null);
  error_occurred_ &= api_is_null;

  bool return_val = error_occurred_ && !timed_out && !received_gain_callback_;

  received_gain_callback_ = false;
  return return_val;
}

// When we don't expect a callback on a separate interface's gain_control.
bool GainControlTestBase::ReceiveNoSecondaryCallback() {
  bool timed_out = RunLoopWithTimeout(kDurationTimeoutExpected);
  EXPECT_TRUE(gain_control_2_.is_bound());

  // For this check, one and only one of these 3 should always be true.
  EXPECT_TRUE(timed_out) << kNoTimeoutErr;
  EXPECT_FALSE(error_occurred_2_) << kConnectionErr;
  EXPECT_FALSE(received_gain_callback_2_);

  bool return_val =
      timed_out && !error_occurred_2_ && !received_gain_callback_2_;

  received_gain_callback_2_ = false;
  return return_val;
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

// For RenderGainControlTest_Negative/CaptureGainControlTest_Negative.
//
// Setting gain too high should cause a disconnect.
void GainControlTestBase::TestSetGainTooHigh() {
  constexpr float expect_gain_db = kTooHighGainDb;
  SetGain(expect_gain_db);

  EXPECT_TRUE(ReceiveDisconnectCallback()) << "API binding did not disconnect!";
  EXPECT_TRUE(gain_control_.is_bound());

  EXPECT_TRUE(ReceiveDisconnectCallback()) << "GainControl did not disconnect!";
  EXPECT_FALSE(gain_control_.is_bound());
}

// Setting gain too low should cause a disconnect.
void GainControlTestBase::TestSetGainTooLow() {
  constexpr float expect_gain_db = kTooLowGainDb;
  SetGain(expect_gain_db);

  EXPECT_TRUE(ReceiveDisconnectCallback()) << "API binding did not disconnect!";
  EXPECT_TRUE(gain_control_.is_bound());

  EXPECT_TRUE(ReceiveDisconnectCallback()) << "GainControl did not disconnect!";
  EXPECT_FALSE(gain_control_.is_bound());
}

//
// Basic GainControl validation with single instance.
//

// RenderGainControlTest
//
void RenderGainControlTest::SetUp() {
  GainControlTestBase::SetUp();
  if (!AudioIsBound())
    return;

  SetUpRenderer();
  SetUpGainControlOnRenderer();

  // Give interfaces a chance to disconnect if they must.
  EXPECT_TRUE(ReceiveNoGainCallback());
}

// Single renderer with one gain control: Gain, Mute and GainMute combo.
//
TEST_F(RenderGainControlTest, SetGain) { TestSetGain(); }
TEST_F(RenderGainControlTest, SetMute) { TestSetMute(); }
TEST_F(RenderGainControlTest, SetGainMute) { TestSetGainMute(); }
// TODO(mpuryear): Ramp-related tests (render). Relevant FIDL signature is:
//   SetGainWithRamp(float32 gain_db, int64 duration_ns, AudioRamp rampType);
// TODO(mpuryear): Validate GainChange notifications of gainramps.
TEST_F(RenderGainControlTest, DuplicateSetGain) { TestDuplicateSetGain(); }
TEST_F(RenderGainControlTest, DuplicateSetMute) { TestDuplicateSetMute(); }

// CaptureGainControlTest
//
void CaptureGainControlTest::SetUp() {
  GainControlTestBase::SetUp();
  if (!AudioIsBound())
    return;

  SetUpCapturer();
  SetUpGainControlOnCapturer();

  // Give interfaces a chance to disconnect if they must.
  EXPECT_TRUE(ReceiveNoGainCallback());
}

// Single capturer with one gain control
//
TEST_F(CaptureGainControlTest, SetGain) { TestSetGain(); }
TEST_F(CaptureGainControlTest, SetMute) { TestSetMute(); }
TEST_F(CaptureGainControlTest, SetGainMute) { TestSetGainMute(); }
// TODO(mpuryear): Ramp-related tests (capture)
TEST_F(CaptureGainControlTest, DuplicateSetGain) { TestDuplicateSetGain(); }
TEST_F(CaptureGainControlTest, DuplicateSetMute) { TestDuplicateSetMute(); }

// RenderGainControlTest_Negative
// Specialization when we expect GainControl/AudioRenderer to disconnect.
//
void RenderGainControlTest_Negative::SetUp() {
  RenderGainControlTest::SetUp();
  if (!AudioIsBound())
    return;

  expect_null_api_ = true;
  expect_error_ = true;
  expect_null_gain_control_ = true;
}

TEST_F(RenderGainControlTest_Negative, SetGainTooHigh) { TestSetGainTooHigh(); }
TEST_F(RenderGainControlTest_Negative, SetGainTooLow) { TestSetGainTooLow(); }
// TODO(mpuryear): Ramp-related negative tests, across all scenarios

// CaptureGainControlTest_Negative
// Specialization when we expect GainControl/AudioCapturer to disconnect.
//
void CaptureGainControlTest_Negative::SetUp() {
  CaptureGainControlTest::SetUp();
  if (!AudioIsBound())
    return;

  expect_null_api_ = true;
  expect_error_ = true;
  expect_null_gain_control_ = true;
}

TEST_F(CaptureGainControlTest_Negative, SetGainTooHigh) {
  TestSetGainTooHigh();
}
TEST_F(CaptureGainControlTest_Negative, SetGainTooLow) { TestSetGainTooLow(); }

// SiblingGainControlsTest
// On a renderer/capturer, sibling GainControls receive identical notifications.
//
// Tests expect a gain callback, and no error. Parent base class absorbs a
// callback from gain_control_, with related error checking. To cleanly
// augment this, we absorb another callback, from the sibling gain control,
// and perform additional error checking related to gain_control_2_.
bool SiblingGainControlsTest::ReceiveGainCallback(float gain_db, bool mute) {
  bool timed_out = RunLoopWithTimeout(kDurationResponseExpected);

  // After wait, ONE of the two GainControls should have received a callback.
  EXPECT_TRUE(gain_control_.is_bound() && gain_control_2_.is_bound());

  EXPECT_TRUE(received_gain_callback_ || received_gain_callback_2_);
  EXPECT_FALSE(timed_out) << kTimeoutErr;
  EXPECT_TRUE(!error_occurred_ && !error_occurred_2_) << kConnectionErr;

  // After this second wait, both of them should be satisfied.
  if (!GainControlTestBase::ReceiveGainCallback(gain_db, mute)) {
    return false;
  }

  EXPECT_TRUE(gain_control_2_.is_bound());

  // For this check, one and only one of these 3 should always be true.
  EXPECT_TRUE(received_gain_callback_2_);
  EXPECT_FALSE(timed_out);
  EXPECT_FALSE(error_occurred_2_);

  EXPECT_EQ(received_gain_db_2_, gain_db);
  EXPECT_EQ(received_mute_2_, mute);

  bool return_val =
      received_gain_callback_2_ && !error_occurred_2_ && !timed_out;
  return_val &= (received_gain_db_2_ == gain_db) && (received_mute_2_ == mute);

  received_gain_callback_2_ = false;
  return return_val;
}

// Tests expect to receive neither gain callback nor error. Parent base class
// asserts this. To augment this, we simply perform additional error checking
// related to gain_control_2_ (error_occurred_2_, received_gain_callback_2_).
bool SiblingGainControlsTest::ReceiveNoGainCallback() {
  bool return_val = GainControlTestBase::ReceiveNoGainCallback();
  EXPECT_TRUE(gain_control_2_.is_bound());

  // We expect to have timed out, so these others should never be true.
  EXPECT_FALSE(error_occurred_2_) << kConnectionErr;
  EXPECT_FALSE(received_gain_callback_2_) << kNoTimeoutErr;

  return_val &= !error_occurred_2_ && !received_gain_callback_2_;

  received_gain_callback_2_ = false;
  return return_val;
}

// Tests expect to receive a disconnect callback for the API binding, then
// another for the GainControl binding. Parent base class asserts that after
// the first disconnect, the GainControl is still bound. To cleanly augment
// this, we should absorb the second disconnect as well -- but only if the
// first disconnect has already occurred. Also perform additional disconnect
// error checking related to the second gain control.
bool SiblingGainControlsTest::ReceiveDisconnectCallback() {
  bool timed_out;
  bool first_disconnect_wait = !ApiIsNull();

  // If GainControl causes disconnect, renderer/capturer disconnects first.
  // Then, we disconnect them one-by-one, TODAY in chronological order (but that
  // might change in the future depending on FIDL BindingSet). In all, we expect
  // three disconnects, but either GainControl could disconnect: allow either.

  // If API has not yet disconnected, expect it (that gains are still intact).
  if (first_disconnect_wait) {
    if (!GainControlTestBase::ReceiveDisconnectCallback()) {
      return false;
    }

    EXPECT_TRUE(gain_control_2_.is_bound());

    EXPECT_FALSE(error_occurred_2_);
    EXPECT_FALSE(received_gain_callback_2_);

    return gain_control_2_.is_bound() && !received_gain_callback_2_ &&
           !error_occurred_2_;
  }

  // If API has already disconnected, wait for a GainControl disconnect.
  timed_out = RunLoopWithTimeout(kDurationResponseExpected);

  // After wait, ONE of the two GainControls should have disconnected.
  EXPECT_TRUE(!gain_control_.is_bound() || !gain_control_2_.is_bound());

  EXPECT_TRUE(error_occurred_ || error_occurred_2_);
  EXPECT_FALSE(timed_out);
  EXPECT_TRUE(!received_gain_callback_ && !received_gain_callback_2_);

  // Now wait for the second disconnect.
  if (!GainControlTestBase::ReceiveDisconnectCallback()) {
    return false;
  }

  EXPECT_FALSE(gain_control_2_.is_bound());

  EXPECT_TRUE(error_occurred_2_);
  EXPECT_FALSE(received_gain_callback_2_);

  // For this check, one and only one of these 3 should always be true.
  bool return_val =
      error_occurred_2_ && !timed_out && !received_gain_callback_2_;

  received_gain_callback_2_ = false;
  return return_val;
}

// RendererTwoGainControlsTest
// Renderer with two gain controls: both should receive identical notifications.
//
void RendererTwoGainControlsTest::SetUp() {
  SiblingGainControlsTest::SetUp();
  if (!AudioIsBound())
    return;

  SetUpRenderer();
  SetUpGainControlOnRenderer();
  SetUpGainControl2OnRenderer();
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
TEST_F(RendererTwoGainControlsTest, DuplicateSetMute) {
  TestDuplicateSetMute();
}

// CapturerTwoGainControlsTest
// Capturer with two gain controls: both should receive identical notifications.
//
void CapturerTwoGainControlsTest::SetUp() {
  SiblingGainControlsTest::SetUp();
  if (!AudioIsBound())
    return;

  SetUpCapturer();
  SetUpGainControlOnCapturer();
  SetUpGainControl2OnCapturer();
}

TEST_F(CapturerTwoGainControlsTest, BothControlsReceiveGainNotifications) {
  TestSetGain();
}
TEST_F(CapturerTwoGainControlsTest, BothControlsReceiveMuteNotifications) {
  TestSetMute();
}
TEST_F(CapturerTwoGainControlsTest, DuplicateSetGain) {
  TestDuplicateSetGain();
}
TEST_F(CapturerTwoGainControlsTest, DuplicateSetMute) {
  TestDuplicateSetMute();
}

// RendererTwoGainControlsTest_Negative
// Specialization when we expect GainControls and Renderer to disconnect.
//
void RendererTwoGainControlsTest_Negative::SetUp() {
  RendererTwoGainControlsTest::SetUp();
  if (!AudioIsBound())
    return;

  expect_null_api_ = true;
  expect_null_gain_control_ = true;
  expect_null_gain_control_2_ = true;
  expect_error_ = true;
  expect_error_2_ = true;
}

TEST_F(RendererTwoGainControlsTest_Negative, SetGainTooHigh) {
  TestSetGainTooHigh();
}
TEST_F(RendererTwoGainControlsTest_Negative, SetGainTooLow) {
  TestSetGainTooLow();
}

// CapturerTwoGainControlsTest_Negative
// Specialization when we expect GainControls and Capturer to disconnect.
//
void CapturerTwoGainControlsTest_Negative::SetUp() {
  CapturerTwoGainControlsTest::SetUp();
  if (!AudioIsBound())
    return;

  expect_null_api_ = true;
  expect_null_gain_control_ = true;
  expect_null_gain_control_2_ = true;
  expect_error_ = true;
  expect_error_2_ = true;
}

TEST_F(CapturerTwoGainControlsTest_Negative, SetGainTooHigh) {
  TestSetGainTooHigh();
}
TEST_F(CapturerTwoGainControlsTest_Negative, SetGainTooLow) {
  TestSetGainTooLow();
}

// IndependentGainControlsTest
// Verify that GainControls on different API instances are fully independent.
//

// Tests expect a gain callback, and no error. Parent base class absorbs a
// callback from gain_control_, with related error checking. To cleanly
// augment this, we simply expect no additional callback (nor disconnect) from
// the independent gain control, with additional related error checking.
bool IndependentGainControlsTest::ReceiveGainCallback(float gain_db,
                                                      bool mute) {
  if (!GainControlTestBase::ReceiveGainCallback(gain_db, mute)) {
    return false;
  }

  return ReceiveNoSecondaryCallback();
}

// Tests expect to receive neither gain callback nor error. Parent base class
// asserts this. To cleanly augment this, we simply expect no additional
// callback (nor disconnect) from the independent gain control.
bool IndependentGainControlsTest::ReceiveNoGainCallback() {
  if (!GainControlTestBase::ReceiveNoGainCallback()) {
    return false;
  }

  return ReceiveNoSecondaryCallback();
}

// Tests expect to receive a disconnect callback for the API binding, then
// another for the GainControl binding. Parent base class asserts that after
// the first disconnect, the GainControl is still bound. To cleanly augment
// this, we should expect NO disconnect from our independent gain control --
// but only after the first gain control disconnect has already occurred.
bool IndependentGainControlsTest::ReceiveDisconnectCallback() {
  if (!GainControlTestBase::ReceiveDisconnectCallback()) {
    return false;
  }

  // If we disconnect but gain_control_  still exists, only the API has been
  // reset. In that case, we might still get other callbacks.
  if (gain_control_.is_bound()) {
    return true;
  }

  // If gain_control_ ALSO disconnected, we should receive no further callback
  return ReceiveNoSecondaryCallback();
}

// TwoRenderersGainControlsTest
// Two renderers, each with a gain control: we expect no cross-impact.
//
void TwoRenderersGainControlsTest::SetUp() {
  IndependentGainControlsTest::SetUp();
  if (!AudioIsBound())
    return;

  SetUpRenderer();
  SetUpGainControlOnRenderer();

  SetUpRenderer2();
  SetUpGainControl2OnRenderer2();
}

TEST_F(TwoRenderersGainControlsTest, OtherInstanceReceivesNoGainNotification) {
  TestSetGain();
}
TEST_F(TwoRenderersGainControlsTest, OtherInstanceReceivesNoMuteNotification) {
  TestSetMute();
}

// RendererCapturerGainControlsTest
// Renderer gain control should not affect capturer gain control.
//
void RendererCapturerGainControlsTest::SetUp() {
  IndependentGainControlsTest::SetUp();
  if (!AudioIsBound())
    return;

  SetUpRenderer();
  SetUpGainControlOnRenderer();

  SetUpCapturer();
  SetUpGainControl2OnCapturer();
}

TEST_F(RendererCapturerGainControlsTest,
       OtherInstanceReceivesNoGainNotification) {
  TestSetGain();
}
TEST_F(RendererCapturerGainControlsTest,
       OtherInstanceReceivesNoMuteNotification) {
  TestSetMute();
}

// CapturerRendererGainControlsTest
// Capturer gain control should not affect renderer gain control.
//
void CapturerRendererGainControlsTest::SetUp() {
  IndependentGainControlsTest::SetUp();
  if (!AudioIsBound())
    return;

  SetUpCapturer();
  SetUpGainControlOnCapturer();

  SetUpRenderer();
  SetUpGainControl2OnRenderer();
}

TEST_F(CapturerRendererGainControlsTest,
       OtherInstanceReceivesNoGainNotification) {
  TestSetGain();
}
TEST_F(CapturerRendererGainControlsTest,
       OtherInstanceReceivesNoMuteNotification) {
  TestSetMute();
}

// TwoCapturersGainControlsTest
// Two capturers, each with a gain control: we expect no cross-impact.
//
void TwoCapturersGainControlsTest::SetUp() {
  IndependentGainControlsTest::SetUp();
  if (!AudioIsBound())
    return;

  SetUpCapturer();
  SetUpGainControlOnCapturer();

  SetUpCapturer2();
  SetUpGainControl2OnCapturer2();
}

TEST_F(TwoCapturersGainControlsTest, OtherInstanceReceivesNoGainNotification) {
  TestSetGain();
}
TEST_F(TwoCapturersGainControlsTest, OtherInstanceReceivesNoMuteNotification) {
  TestSetMute();
}

// TwoRenderersGainControlsTest_Negative
// Specialization when we expect one GainControl/Renderer to disconnect.
//
void TwoRenderersGainControlsTest_Negative::SetUp() {
  TwoRenderersGainControlsTest::SetUp();
  if (!AudioIsBound())
    return;

  expect_null_api_ = true;
  expect_error_ = true;
  expect_null_gain_control_ = true;
}

TEST_F(TwoRenderersGainControlsTest_Negative, SetGainTooHigh) {
  TestSetGainTooHigh();
}
TEST_F(TwoRenderersGainControlsTest_Negative, SetGainTooLow) {
  TestSetGainTooLow();
}

// RendererCapturerGainControlsTest_Negative
// Specialization when we expect one GainControl/Renderer to disconnect.
//
void RendererCapturerGainControlsTest_Negative::SetUp() {
  RendererCapturerGainControlsTest::SetUp();
  if (!AudioIsBound())
    return;

  expect_null_api_ = true;
  expect_error_ = true;
  expect_null_gain_control_ = true;
}

TEST_F(RendererCapturerGainControlsTest_Negative, SetGainTooHigh) {
  TestSetGainTooHigh();
}
TEST_F(RendererCapturerGainControlsTest_Negative, SetGainTooLow) {
  TestSetGainTooLow();
}

// CapturerRendererGainControlsTest_Negative
// Specialization when we expect one GainControl/Capturer to disconnect.
//
void CapturerRendererGainControlsTest_Negative::SetUp() {
  CapturerRendererGainControlsTest::SetUp();
  if (!AudioIsBound())
    return;

  expect_null_api_ = true;
  expect_error_ = true;
  expect_null_gain_control_ = true;
}

TEST_F(CapturerRendererGainControlsTest_Negative, SetGainTooHigh) {
  TestSetGainTooHigh();
}
TEST_F(CapturerRendererGainControlsTest_Negative, SetGainTooLow) {
  TestSetGainTooLow();
}

// TwoCapturersGainControlsTest_Negative
// Specialization when we expect one GainControl/Capturer to disconnect.
//
void TwoCapturersGainControlsTest_Negative::SetUp() {
  TwoCapturersGainControlsTest::SetUp();
  if (!AudioIsBound())
    return;

  expect_null_api_ = true;
  expect_error_ = true;
  expect_null_gain_control_ = true;
}

TEST_F(TwoCapturersGainControlsTest_Negative, SetGainTooHigh) {
  TestSetGainTooHigh();
}
TEST_F(TwoCapturersGainControlsTest_Negative, SetGainTooLow) {
  TestSetGainTooLow();
}

}  // namespace test
}  // namespace audio
}  // namespace media
