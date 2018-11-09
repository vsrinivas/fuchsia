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
  void SetUp() override {
    ::gtest::RealLoopFixture::SetUp();

    environment_services_ = component::GetEnvironmentServices();
    environment_services_->ConnectToService(audio_.NewRequest());
    ASSERT_TRUE(audio_);

    audio_.set_error_handler([this]() {
      error_occurred_ = true;
      QuitLoop();
    });

    audio_->CreateAudioRenderer(audio_renderer_.NewRequest());
    ASSERT_TRUE(audio_renderer_);

    audio_renderer_.set_error_handler([this]() {
      error_occurred_ = true;
      QuitLoop();
    });

    audio_renderer_->BindGainControl(ar_gain_control_.NewRequest());

    ar_gain_control_.set_error_handler([this]() {
      error_occurred_ = true;
      QuitLoop();
    });

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

  virtual void TearDown() override {
    EXPECT_FALSE(error_occurred_);

    ar_gain_control_.Unbind();
    audio_renderer_.Unbind();
    audio_.Unbind();

    ::gtest::RealLoopFixture::TearDown();
  }

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
// Slight specialization, for test cases that expect the binding to disconnect.
class GainControlTest_Negative : public GainControlTest {
 protected:
  void TearDown() override {
    EXPECT_TRUE(error_occurred_);

    ar_gain_control_.Unbind();
    audio_renderer_.Unbind();
    audio_.Unbind();

    ::gtest::RealLoopFixture::TearDown();
  }
};

/*
0x0101: SetGain(float32 gain_db);
0x0102: SetGainWithRamp(float32 gain_db,
                        int64 duration_ns,
                        AudioRamp rampType);
0x0103: SetMute(bool muted);
0x0104: -> OnGainMuteChanged(float32 gain_db, bool muted);
*/

//
// GainControl validation (from AudioRenderer binding)
//
// Gain-related tests
TEST_F(GainControlTest, SetRenderGain) {
  constexpr float expect_gain_db = 20.0f;
  ar_gain_control_->SetGain(expect_gain_db);

  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  EXPECT_TRUE(ar_gain_control_);
  EXPECT_TRUE(received_gain_callback_);
  EXPECT_EQ(received_gain_db_, expect_gain_db);
}

// Mute-related tests
TEST_F(GainControlTest, SetRenderMute) {
  float expect_mute = true;
  ar_gain_control_->SetMute(expect_mute);

  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  EXPECT_TRUE(ar_gain_control_);
  EXPECT_TRUE(received_gain_callback_);
  EXPECT_EQ(received_mute_, expect_mute);

  expect_mute = false;
  ar_gain_control_->SetMute(expect_mute);

  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  EXPECT_TRUE(ar_gain_control_);
  EXPECT_TRUE(received_gain_callback_);
  EXPECT_EQ(received_mute_, expect_mute);
}

// Gain-mute interaction tests
TEST_F(GainControlTest, SetRenderGainMute) {
  constexpr float expect_gain_db = -5.5f;
  constexpr float expect_mute = true;
  ar_gain_control_->SetGain(expect_gain_db);
  ar_gain_control_->SetMute(expect_mute);

  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  EXPECT_TRUE(received_gain_callback_);

  received_gain_callback_ = false;
  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  EXPECT_TRUE(ar_gain_control_);
  EXPECT_TRUE(received_gain_callback_);
  EXPECT_EQ(received_gain_db_, expect_gain_db);
  EXPECT_EQ(received_mute_, expect_mute);
}

// TODO(mpuryear): Ramp-related tests

// Callback-related tests
TEST_F(GainControlTest, SetDuplicateRenderMute) {
  float expect_mute = true;
  ar_gain_control_->SetMute(expect_mute);

  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  EXPECT_TRUE(ar_gain_control_);
  EXPECT_TRUE(received_gain_callback_);
  EXPECT_EQ(received_mute_, expect_mute);

  received_gain_callback_ = false;
  ar_gain_control_->SetMute(expect_mute);

  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));
  EXPECT_TRUE(ar_gain_control_);
  EXPECT_FALSE(received_gain_callback_);
}

TEST_F(GainControlTest, SetDuplicateRenderGain) {
  constexpr float expect_gain_db = 20.0f;
  ar_gain_control_->SetGain(expect_gain_db);

  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  EXPECT_TRUE(ar_gain_control_);
  EXPECT_TRUE(received_gain_callback_);
  EXPECT_EQ(received_gain_db_, expect_gain_db);

  received_gain_callback_ = false;
  ar_gain_control_->SetGain(expect_gain_db);

  EXPECT_TRUE(RunLoopWithTimeout(kDurationTimeoutExpected));
  EXPECT_TRUE(ar_gain_control_);
  EXPECT_FALSE(received_gain_callback_);
}

//
// TODO(mpuryear): need to validate GainControl from the capturer side
// GainControl validation (from AudioCapturer binding)
//

/*
0x0101: SetGain(float32 gain_db);
0x0102: SetGainWithRamp(float32 gain_db,
                        int64 duration_ns,
                        AudioRamp rampType);
0x0103: SetMute(bool muted);
0x0104: -> OnGainMuteChanged(float32 gain_db, bool muted);
*/

//
// GainControl negative validation
//
// Setting renderer gain too high should cause a disconnect.
TEST_F(GainControlTest_Negative, SetRenderGainTooHigh) {
  constexpr float expect_gain_db = kTooHighGainDb;
  ar_gain_control_->SetGain(expect_gain_db);

  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  ASSERT_TRUE(error_occurred_);
  EXPECT_FALSE(received_gain_callback_);

  // Give the ar_gain_control_ a chance to be destroyed
  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  EXPECT_FALSE(ar_gain_control_);
  EXPECT_FALSE(audio_renderer_);
}

// Setting renderer gain too low should cause a disconnect.
TEST_F(GainControlTest_Negative, SetRenderGainTooLow) {
  constexpr float expect_gain_db = kTooLowGainDb;
  ar_gain_control_->SetGain(expect_gain_db);

  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  ASSERT_TRUE(error_occurred_);
  EXPECT_FALSE(received_gain_callback_);

  // Give the ar_gain_control_ a chance to be destroyed
  EXPECT_FALSE(RunLoopWithTimeout(kDurationResponseExpected));
  EXPECT_FALSE(ar_gain_control_);
  EXPECT_FALSE(audio_renderer_);
}

// TODO(mpuryear): capturer gain/mute-related negative tests
// TODO(mpuryear): Ramp-related negative tests

}  // namespace test
}  // namespace audio
}  // namespace media
