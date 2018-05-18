// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/gtest/test_with_message_loop.h>
#include <media/cpp/fidl.h>
#include <cmath>
#include "lib/app/cpp/environment_services.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "lib/fxl/logging.h"

namespace media {
namespace audio {
namespace test {

//
// Tests of the asynchronous AudioServer interface.
//
class AudioServerTest : public gtest::TestWithMessageLoop {
 protected:
  void SetUp() override {
    component::ConnectToEnvironmentService(audio_server_.NewRequest());
    ASSERT_TRUE(audio_server_);

    audio_server_.set_error_handler([this]() {
      FXL_LOG(ERROR) << "AudioServer connection lost. Quitting.";
      error_occurred_ = true;
      message_loop_.PostQuitTask();
    });
  }

  void TearDown() override { EXPECT_FALSE(error_occurred_); }

  AudioServerPtr audio_server_;
  AudioRendererPtr audio_renderer_;
  MediaRendererPtr media_renderer_;
  AudioRenderer2Ptr audio_renderer2_;
  AudioCapturerPtr audio_capturer_;

  bool error_occurred_ = false;
};

// Test creation and interface independence of AudioRenderer and MediaRenderer.
TEST_F(AudioServerTest, CreateRenderer) {
  // Validate AudioServer can create AudioRenderer and MediaRenderer interfaces.
  audio_server_->CreateRenderer(audio_renderer_.NewRequest(),
                                media_renderer_.NewRequest());
  EXPECT_TRUE(audio_renderer_);
  EXPECT_TRUE(media_renderer_);

  // Validate that MediaRenderer persists without AudioRenderer.
  audio_renderer_.Unbind();
  EXPECT_FALSE(audio_renderer_);
  EXPECT_TRUE(media_renderer_);
  EXPECT_TRUE(audio_server_);

  // Validate that AudioServer persists without AudioRenderer or MediaRenderer.
  media_renderer_.Unbind();
  EXPECT_FALSE(media_renderer_);
  EXPECT_TRUE(audio_server_);

  // Validate that these interfaces persist after AudioServer is unbound.
  audio_server_->CreateRenderer(audio_renderer_.NewRequest(),
                                media_renderer_.NewRequest());
  audio_server_.Unbind();
  EXPECT_FALSE(audio_server_);
  EXPECT_TRUE(audio_renderer_);
  EXPECT_TRUE(media_renderer_);
}

// Test creation and interface independence of AudioRenderer2.
TEST_F(AudioServerTest, CreateRenderer2) {
  // Validate AudioServer can create AudioRenderer2 interface.
  audio_server_->CreateRendererV2(audio_renderer2_.NewRequest());
  EXPECT_TRUE(audio_renderer2_);

  // Validate that AudioServer persists without AudioRenderer2.
  audio_renderer2_.Unbind();
  EXPECT_FALSE(audio_renderer2_);
  EXPECT_TRUE(audio_server_);

  // Validate AudioRenderer2 persists after AudioServer is unbound.
  audio_server_->CreateRendererV2(audio_renderer2_.NewRequest());
  audio_server_.Unbind();
  EXPECT_FALSE(audio_server_);
  EXPECT_TRUE(audio_renderer2_);
}

// Test creation and interface independence of AudioCapturer.
TEST_F(AudioServerTest, CreateCapturer) {
  // Validate AudioServer can create AudioCapturer interface.
  audio_server_->CreateCapturer(audio_capturer_.NewRequest(), false);
  EXPECT_TRUE(audio_capturer_);

  // Validate that AudioServer persists without AudioCapturer.
  audio_capturer_.Unbind();
  EXPECT_FALSE(audio_capturer_);
  EXPECT_TRUE(audio_server_);

  // Validate AudioCapturer persists after AudioServer is unbound.
  audio_server_->CreateCapturer(audio_capturer_.NewRequest(), true);
  audio_server_.Unbind();
  EXPECT_FALSE(audio_server_);
  EXPECT_TRUE(audio_capturer_);
}

// Test setting and getting of master gain.
TEST_F(AudioServerTest, MasterGain) {
  constexpr float kMasterGainVal = -20.0f;

  // Validate that master gain can be retrieved.
  float gain_db = NAN;
  audio_server_->GetMasterGain([this, &gain_db](float current_gain_db) {
    gain_db = current_gain_db;
    message_loop_.PostQuitTask();
  });

  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_FALSE(error_occurred_);
  EXPECT_FALSE(isnan(gain_db));
  ASSERT_TRUE(audio_server_);

  // Validate that master gain is set. Correctly wait for its retrieval.
  // loop.ResetQuit();
  audio_server_->SetMasterGain(kMasterGainVal);
  audio_server_->GetMasterGain([this, &gain_db](float current_gain_db) {
    gain_db = current_gain_db;
    message_loop_.PostQuitTask();
  });

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(kMasterGainVal, gain_db);
}

//
// Tests of the synchronous AudioServerSync interface.
//
// We expect the async and sync interfaces to track each other exactly -- any
// behavior otherwise is a bug in core FIDL. These tests were only created to
// better understand how errors manifest themselves when using sync interfaces.
// In short, further testing of the sync interfaces (over and above any testing
// done on the async interfaces) should not be needed.
//
class AudioServerSyncTest : public gtest::TestWithMessageLoop {
 protected:
  void SetUp() override {
    component::ConnectToEnvironmentService(audio_server_.NewRequest());
    ASSERT_TRUE(audio_server_);
  }

  AudioServerSyncPtr audio_server_;
  AudioRendererSyncPtr audio_renderer_;
  MediaRendererSyncPtr media_renderer_;
  AudioRenderer2SyncPtr audio_renderer2_;
  AudioCapturerSyncPtr audio_capturer_;
};

// Test creation and survival of synchronous AudioRenderer and MediaRenderer.
TEST_F(AudioServerSyncTest, CreateRenderer) {
  // Validate AudioServer can create AudioRenderer and MediaRenderer interfaces.
  EXPECT_TRUE(audio_server_->CreateRenderer(audio_renderer_.NewRequest(),
                                            media_renderer_.NewRequest()));
  EXPECT_TRUE(audio_renderer_);
  EXPECT_TRUE(media_renderer_);

  // Validate that AudioRenderer persists without MediaRenderer.
  media_renderer_ = nullptr;
  EXPECT_TRUE(audio_renderer_);

  // Validate that AudioServer persists without AudioRenderer or MediaRenderer.
  audio_renderer_ = nullptr;
  ASSERT_TRUE(audio_server_);

  // Validate that these interfaces persist after AudioServer is unbound.
  EXPECT_TRUE(audio_server_->CreateRenderer(audio_renderer_.NewRequest(),
                                            media_renderer_.NewRequest()));
  audio_server_ = nullptr;
  EXPECT_TRUE(audio_renderer_);
  EXPECT_TRUE(media_renderer_);
}

// Test creation and interface independence of AudioRenderer2.
TEST_F(AudioServerSyncTest, CreateRenderer2) {
  // Validate AudioServer can create AudioRenderer2 interface.
  EXPECT_TRUE(audio_server_->CreateRendererV2(audio_renderer2_.NewRequest()));
  EXPECT_TRUE(audio_renderer2_);

  // Validate that AudioServer persists without AudioRenderer2.
  audio_renderer2_ = nullptr;
  ASSERT_TRUE(audio_server_);

  // Validate AudioRenderer2 persists after AudioServer is unbound.
  EXPECT_TRUE(audio_server_->CreateRendererV2(audio_renderer2_.NewRequest()));
  audio_server_ = nullptr;
  EXPECT_TRUE(audio_renderer2_);
}

// Test creation and interface independence of AudioCapturer.
TEST_F(AudioServerSyncTest, CreateCapturer) {
  // Validate AudioServer can create AudioCapturer interface.
  EXPECT_TRUE(
      audio_server_->CreateCapturer(audio_capturer_.NewRequest(), true));
  EXPECT_TRUE(audio_capturer_);

  // Validate that AudioServer persists without AudioCapturer.
  audio_capturer_ = nullptr;
  ASSERT_TRUE(audio_server_);

  // Validate AudioCapturer persists after AudioServer is unbound.
  audio_server_->CreateCapturer(audio_capturer_.NewRequest(), false);
  audio_server_ = nullptr;
  EXPECT_TRUE(audio_capturer_);
}

// Test setting and getting of master gain.
TEST_F(AudioServerSyncTest, MasterGain) {
  // Validate that master gain can be retrieved.
  float gain_db = NAN;
  EXPECT_TRUE(audio_server_->GetMasterGain(&gain_db));
  EXPECT_FALSE(isnan(gain_db));

  // Validate that master gain can be set.
  constexpr float kExpectedVal = -5.0f;
  EXPECT_TRUE(audio_server_->SetMasterGain(kExpectedVal));

  // Validate that master gain was indeed persistently set.
  EXPECT_TRUE(audio_server_->GetMasterGain(&gain_db));
  EXPECT_EQ(kExpectedVal, gain_db);
}

// TODO(mpuryear): If there is ever additional functionality associated with
// main (such as parameter parsing), relocate all of the AudioServer and
// AudioServerSync tests to a separate audio_server_tests.cc file.
//
int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  int result = RUN_ALL_TESTS();

  return result;
}

}  // namespace test
}  // namespace audio
}  // namespace media
