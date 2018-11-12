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
// GainControlTest
//
// This set of tests verifies asynchronous usage of GainControl.
class GainControlTest : public gtest::RealLoopFixture {
 protected:
  void SetUp() override;
  virtual void TearDown() override;

  void SetGain(float gain_db);
  void SetMute(bool mute);
  bool ReceiveGainCallback(float gain_db, bool mute);
  bool ReceiveNoGainCallback();

  std::shared_ptr<component::Services> environment_services_;
  fuchsia::media::AudioPtr audio_;
  fuchsia::media::AudioRendererPtr audio_renderer_;
  fuchsia::media::GainControlPtr ar_gain_control_;

  bool error_occurred_ = false;

  bool received_gain_callback_ = false;
  float received_gain_db_ = kTooLowGainDb;
  bool received_mute_ = false;
};

//
// GainControlTest_Negative
//
// Slight specialization for tests that expect GainControl and AudioRenderer
// bindings to disconnect.
class GainControlTest_Negative : public GainControlTest {
 protected:
  void TearDown() override {
    EXPECT_TRUE(error_occurred_);
    EXPECT_FALSE(ar_gain_control_);
    EXPECT_FALSE(audio_renderer_);

    EXPECT_TRUE(audio_);

    ::gtest::RealLoopFixture::TearDown();
  }

  bool ReceiveDisconnectCallback();
};

//
// GainControlTest implementation
//
void GainControlTest::SetUp() {
  ::gtest::RealLoopFixture::SetUp();

  environment_services_ = component::GetEnvironmentServices();
  environment_services_->ConnectToService(audio_.NewRequest());
  ASSERT_TRUE(audio_);

  auto err_handler = [this](zx_status_t error) {
    error_occurred_ = true;
    QuitLoop();
  };

  audio_.set_error_handler(err_handler);

  audio_->CreateAudioRenderer(audio_renderer_.NewRequest());
  ASSERT_TRUE(audio_renderer_);

  audio_renderer_.set_error_handler(err_handler);

  audio_renderer_->BindGainControl(ar_gain_control_.NewRequest());
  ASSERT_TRUE(ar_gain_control_);

  ar_gain_control_.set_error_handler(err_handler);

  ar_gain_control_.events().OnGainMuteChanged = [this](float gain_db,
                                                       bool muted) {
    received_gain_callback_ = true;

    received_gain_db_ = gain_db;
    received_mute_ = muted;
    QuitLoop();
  };

  // Give interfaces a chance to disconnect if they must.
  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));
  EXPECT_TRUE(ar_gain_control_);

  EXPECT_FALSE(received_gain_callback_);
}

void GainControlTest::TearDown() {
  EXPECT_FALSE(error_occurred_);

  EXPECT_TRUE(ar_gain_control_);
  EXPECT_TRUE(audio_renderer_);
  EXPECT_TRUE(audio_);

  ::gtest::RealLoopFixture::TearDown();
}

// Set Gain, first resetting state so error can be detected.
void GainControlTest::SetGain(float gain_db) {
  received_gain_callback_ = false;
  ar_gain_control_->SetGain(gain_db);
}

// Set Mute, first resetting state variable so error can be detected.
void GainControlTest::SetMute(bool mute) {
  received_gain_callback_ = false;
  ar_gain_control_->SetMute(mute);
}

// Expecting to receive a callback, wait for it and check for errors.
bool GainControlTest::ReceiveGainCallback(float gain_db, bool mute) {
  bool timed_out = RunLoopWithTimeout(kDurationResponseExpected);
  EXPECT_TRUE(ar_gain_control_);
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
bool GainControlTest::ReceiveNoGainCallback() {
  bool timed_out = RunLoopWithTimeout(kDurationTimeoutExpected);
  EXPECT_TRUE(ar_gain_control_);
  EXPECT_FALSE(error_occurred_) << kConnectionErr;
  EXPECT_FALSE(received_gain_callback_) << kNoTimeoutErr;
  EXPECT_TRUE(timed_out);

  bool return_val = !error_occurred_ && timed_out && !received_gain_callback_;
  received_gain_callback_ = false;
  return return_val;
}

//
// GainControlTest_Negative implementation
//
// Expecting to receive a disconnect callback, wait for it and verify errors.
bool GainControlTest_Negative::ReceiveDisconnectCallback() {
  bool timed_out = RunLoopWithTimeout(kDurationTimeoutExpected);

  EXPECT_TRUE(error_occurred_);
  // Even if a client causes a disconnect by misusing a child GainControl, the
  // AudioRenderer will always disconnect first.
  EXPECT_FALSE(audio_renderer_);
  EXPECT_FALSE(timed_out);
  EXPECT_FALSE(received_gain_callback_);

  return error_occurred_ && !timed_out && !received_gain_callback_ &&
         !audio_renderer_;
}

//
// GainControl validation (from AudioRenderer binding)
//
// Gain-related tests
TEST_F(GainControlTest, SetRenderGain) {
  constexpr float expect_gain_db = 20.0f;
  SetGain(expect_gain_db);
  EXPECT_TRUE(ReceiveGainCallback(expect_gain_db, false));

  SetGain(kUnityGainDb);
  EXPECT_TRUE(ReceiveGainCallback(kUnityGainDb, false));
}

// Mute-related tests
TEST_F(GainControlTest, SetRenderMute) {
  float expect_mute = true;
  SetMute(expect_mute);
  EXPECT_TRUE(ReceiveGainCallback(kUnityGainDb, expect_mute));

  expect_mute = false;
  SetMute(expect_mute);
  EXPECT_TRUE(ReceiveGainCallback(kUnityGainDb, expect_mute));
}

// Gain-mute interaction tests
TEST_F(GainControlTest, SetRenderGainMute) {
  constexpr float expect_gain_db = -5.5f;
  constexpr bool expect_mute = true;

  SetGain(expect_gain_db);
  SetMute(expect_mute);

  EXPECT_TRUE(ReceiveGainCallback(expect_gain_db, false));
  EXPECT_TRUE(ReceiveGainCallback(expect_gain_db, expect_mute));
}

// TODO(mpuryear): Ramp-related tests (render and capture). FIDL signature:
// SetGainWithRamp(float32 gain_db, int64 duration_ns, AudioRamp rampType);

// Callback-related tests
TEST_F(GainControlTest, SetDuplicateRenderMute) {
  float expect_mute = true;
  SetMute(expect_mute);
  EXPECT_TRUE(ReceiveGainCallback(kUnityGainDb, expect_mute));

  SetMute(expect_mute);
  EXPECT_TRUE(ReceiveNoGainCallback());
}

TEST_F(GainControlTest, SetDuplicateRenderGain) {
  constexpr float expect_gain_db = 20.0f;
  SetGain(expect_gain_db);
  EXPECT_TRUE(ReceiveGainCallback(expect_gain_db, false));

  SetGain(expect_gain_db);
  EXPECT_TRUE(ReceiveNoGainCallback());
}

//
// TODO(mpuryear): need to validate GainControl from the capturer side
// GainControl validation (from AudioCapturer binding)
//

//
// GainControl negative validation
//
// Setting renderer gain too high should cause a disconnect.
TEST_F(GainControlTest_Negative, SetRenderGainTooHigh) {
  constexpr float expect_gain_db = kTooHighGainDb;
  SetGain(expect_gain_db);

  EXPECT_TRUE(ReceiveDisconnectCallback()) << "Renderer did not disconnect!";
  EXPECT_TRUE(ar_gain_control_);

  EXPECT_TRUE(ReceiveDisconnectCallback()) << "GainControl did not disconnect!";
  EXPECT_FALSE(ar_gain_control_);
}

// Setting renderer gain too low should cause a disconnect.
TEST_F(GainControlTest_Negative, SetRenderGainTooLow) {
  constexpr float expect_gain_db = kTooLowGainDb;
  SetGain(expect_gain_db);

  EXPECT_TRUE(ReceiveDisconnectCallback()) << "Renderer did not disconnect!";
  EXPECT_TRUE(ar_gain_control_);

  EXPECT_TRUE(ReceiveDisconnectCallback()) << "GainControl did not disconnect!";
  EXPECT_FALSE(ar_gain_control_);
}

// TODO(mpuryear): capturer gain/mute-related negative tests
// TODO(mpuryear): Ramp-related negative tests (render and capture).

}  // namespace test
}  // namespace audio
}  // namespace media
