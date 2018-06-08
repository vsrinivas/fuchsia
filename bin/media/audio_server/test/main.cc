// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmath>

#include <fuchsia/media/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/gtest/test_with_message_loop.h>

#include "lib/app/cpp/environment_services.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "lib/fxl/logging.h"

namespace media {
namespace audio {
namespace test {

//
// Tests of the asynchronous Audio interface.
//
class AudioServerTest : public gtest::TestWithMessageLoop {
 protected:
  void SetUp() override {
    fuchsia::sys::ConnectToEnvironmentService(audio_.NewRequest());
    ASSERT_TRUE(audio_);

    audio_.set_error_handler([this]() {
      FXL_LOG(ERROR) << "Audio connection lost. Quitting.";
      error_occurred_ = true;
      message_loop_.PostQuitTask();
    });
  }

  void TearDown() override { EXPECT_FALSE(error_occurred_); }

  fuchsia::media::AudioPtr audio_;
  fuchsia::media::AudioRendererPtr audio_renderer_;
  fuchsia::media::MediaRendererPtr media_renderer_;
  fuchsia::media::AudioRenderer2Ptr audio_renderer2_;
  fuchsia::media::AudioCapturerPtr audio_capturer_;

  bool error_occurred_ = false;
};

// Test creation and interface independence of AudioRenderer and MediaRenderer.
TEST_F(AudioServerTest, CreateRenderer) {
  // Validate Audio can create AudioRenderer and MediaRenderer interfaces.
  audio_->CreateRenderer(audio_renderer_.NewRequest(),
                         media_renderer_.NewRequest());
  EXPECT_TRUE(audio_renderer_);
  EXPECT_TRUE(media_renderer_);

  // Validate that MediaRenderer persists without AudioRenderer.
  audio_renderer_.Unbind();
  EXPECT_FALSE(audio_renderer_);
  EXPECT_TRUE(media_renderer_);
  EXPECT_TRUE(audio_);

  // Validate that Audio persists without AudioRenderer or MediaRenderer.
  media_renderer_.Unbind();
  EXPECT_FALSE(media_renderer_);
  EXPECT_TRUE(audio_);

  // Validate that these interfaces persist after Audio is unbound.
  audio_->CreateRenderer(audio_renderer_.NewRequest(),
                         media_renderer_.NewRequest());
  audio_.Unbind();
  EXPECT_FALSE(audio_);
  EXPECT_TRUE(audio_renderer_);
  EXPECT_TRUE(media_renderer_);
}

// Test creation and interface independence of AudioRenderer2.
TEST_F(AudioServerTest, CreateRenderer2) {
  // Validate Audio can create AudioRenderer2 interface.
  audio_->CreateRendererV2(audio_renderer2_.NewRequest());
  EXPECT_TRUE(audio_renderer2_);

  // Validate that Audio persists without AudioRenderer2.
  audio_renderer2_.Unbind();
  EXPECT_FALSE(audio_renderer2_);
  EXPECT_TRUE(audio_);

  // Validate AudioRenderer2 persists after Audio is unbound.
  audio_->CreateRendererV2(audio_renderer2_.NewRequest());
  audio_.Unbind();
  EXPECT_FALSE(audio_);
  EXPECT_TRUE(audio_renderer2_);
}

// Test creation and interface independence of AudioCapturer.
TEST_F(AudioServerTest, CreateCapturer) {
  // Validate Audio can create AudioCapturer interface.
  audio_->CreateCapturer(audio_capturer_.NewRequest(), false);
  EXPECT_TRUE(audio_capturer_);

  // Validate that Audio persists without AudioCapturer.
  audio_capturer_.Unbind();
  EXPECT_FALSE(audio_capturer_);
  EXPECT_TRUE(audio_);

  // Validate AudioCapturer persists after Audio is unbound.
  audio_->CreateCapturer(audio_capturer_.NewRequest(), true);
  audio_.Unbind();
  EXPECT_FALSE(audio_);
  EXPECT_TRUE(audio_capturer_);
}

// Test setting and getting of master gain.
TEST_F(AudioServerTest, MasterGain) {
  constexpr float kMasterGainVal = -20.0f;

  // Validate that master gain can be retrieved.
  float gain_db = NAN;
  audio_->GetMasterGain([this, &gain_db](float current_gain_db) {
    gain_db = current_gain_db;
    message_loop_.PostQuitTask();
  });

  EXPECT_FALSE(RunLoopWithTimeout());

  EXPECT_FALSE(error_occurred_);
  EXPECT_FALSE(isnan(gain_db));
  ASSERT_TRUE(audio_);

  // Validate that master gain is set. Correctly wait for its retrieval.
  // loop.ResetQuit();
  audio_->SetMasterGain(kMasterGainVal);
  audio_->GetMasterGain([this, &gain_db](float current_gain_db) {
    gain_db = current_gain_db;
    message_loop_.PostQuitTask();
  });

  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(kMasterGainVal, gain_db);
}

//
// Tests of the synchronous AudioSync interface.
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
    fuchsia::sys::ConnectToEnvironmentService(audio_.NewRequest());
    ASSERT_TRUE(audio_);
  }

  fuchsia::media::AudioSyncPtr audio_;
  fuchsia::media::AudioRendererSyncPtr audio_renderer_;
  fuchsia::media::MediaRendererSyncPtr media_renderer_;
  fuchsia::media::AudioRenderer2SyncPtr audio_renderer2_;
  fuchsia::media::AudioCapturerSyncPtr audio_capturer_;
};

// Test creation and survival of synchronous AudioRenderer and MediaRenderer.
TEST_F(AudioServerSyncTest, CreateRenderer) {
  // Validate Audio can create AudioRenderer and MediaRenderer interfaces.
  EXPECT_TRUE(audio_->CreateRenderer(audio_renderer_.NewRequest(),
                                     media_renderer_.NewRequest()));
  EXPECT_TRUE(audio_renderer_);
  EXPECT_TRUE(media_renderer_);

  // Validate that AudioRenderer persists without MediaRenderer.
  media_renderer_ = nullptr;
  EXPECT_TRUE(audio_renderer_);

  // Validate that Audio persists without AudioRenderer or MediaRenderer.
  audio_renderer_ = nullptr;
  ASSERT_TRUE(audio_);

  // Validate that these interfaces persist after Audio is unbound.
  EXPECT_TRUE(audio_->CreateRenderer(audio_renderer_.NewRequest(),
                                     media_renderer_.NewRequest()));
  audio_ = nullptr;
  EXPECT_TRUE(audio_renderer_);
  EXPECT_TRUE(media_renderer_);
}

// Test creation and interface independence of AudioRenderer2.
TEST_F(AudioServerSyncTest, CreateRenderer2) {
  // Validate Audio can create AudioRenderer2 interface.
  EXPECT_TRUE(audio_->CreateRendererV2(audio_renderer2_.NewRequest()));
  EXPECT_TRUE(audio_renderer2_);

  // Validate that Audio persists without AudioRenderer2.
  audio_renderer2_ = nullptr;
  ASSERT_TRUE(audio_);

  // Validate AudioRenderer2 persists after Audio is unbound.
  EXPECT_TRUE(audio_->CreateRendererV2(audio_renderer2_.NewRequest()));
  audio_ = nullptr;
  EXPECT_TRUE(audio_renderer2_);
}

// Test creation and interface independence of AudioCapturer.
TEST_F(AudioServerSyncTest, CreateCapturer) {
  // Validate Audio can create AudioCapturer interface.
  EXPECT_TRUE(audio_->CreateCapturer(audio_capturer_.NewRequest(), true));
  EXPECT_TRUE(audio_capturer_);

  // Validate that Audio persists without AudioCapturer.
  audio_capturer_ = nullptr;
  ASSERT_TRUE(audio_);

  // Validate AudioCapturer persists after Audio is unbound.
  audio_->CreateCapturer(audio_capturer_.NewRequest(), false);
  audio_ = nullptr;
  EXPECT_TRUE(audio_capturer_);
}

// Test setting and getting of master gain.
TEST_F(AudioServerSyncTest, MasterGain) {
  // Validate that master gain can be retrieved.
  float gain_db = NAN;
  EXPECT_TRUE(audio_->GetMasterGain(&gain_db));
  EXPECT_FALSE(isnan(gain_db));

  // Validate that master gain can be set.
  constexpr float kExpectedVal = -5.0f;
  EXPECT_TRUE(audio_->SetMasterGain(kExpectedVal));

  // Validate that master gain was indeed persistently set.
  EXPECT_TRUE(audio_->GetMasterGain(&gain_db));
  EXPECT_EQ(kExpectedVal, gain_db);
}

// TODO(mpuryear): If there is ever additional functionality associated with
// main (such as parameter parsing), relocate all of the Audio and
// AudioSync tests to a separate audio_server_tests.cc file.
//
int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  int result = RUN_ALL_TESTS();

  return result;
}

}  // namespace test
}  // namespace audio
}  // namespace media
