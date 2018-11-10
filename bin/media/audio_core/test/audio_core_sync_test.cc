// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>

#include <lib/gtest/real_loop_fixture.h>

#include "lib/component/cpp/environment_services_helper.h"

namespace media {
namespace audio {
namespace test {

//
// Tests of the synchronous AudioSync interface.
//
// We expect the async and sync interfaces to track each other exactly -- any
// behavior otherwise is a bug in core FIDL. These tests were only created to
// better understand how errors manifest themselves when using sync interfaces.
// In short, further testing of the sync interfaces (over and above any testing
// done on the async interfaces) should not be needed.
//
class AudioCoreSyncTest : public gtest::RealLoopFixture {
 protected:
  void SetUp() override {
    ::gtest::RealLoopFixture::SetUp();

    environment_services_ = component::GetEnvironmentServices();
    environment_services_->ConnectToService(audio_.NewRequest());
    ASSERT_TRUE(audio_);
  }

  std::shared_ptr<component::Services> environment_services_;
  fuchsia::media::AudioSyncPtr audio_;
  fuchsia::media::AudioRendererSyncPtr audio_renderer_;
  fuchsia::media::AudioCapturerSyncPtr audio_capturer_;
};

// Test creation and interface independence of AudioRenderer.
TEST_F(AudioCoreSyncTest, CreateAudioRenderer) {
  // Validate Audio can create AudioRenderer interface.
  EXPECT_EQ(ZX_OK, audio_->CreateAudioRenderer(audio_renderer_.NewRequest()));
  EXPECT_TRUE(audio_renderer_);

  // Validate that Audio persists without AudioRenderer.
  audio_renderer_ = nullptr;
  ASSERT_TRUE(audio_);

  // Validate AudioRenderer persists after Audio is unbound.
  EXPECT_EQ(ZX_OK, audio_->CreateAudioRenderer(audio_renderer_.NewRequest()));
  audio_ = nullptr;
  EXPECT_TRUE(audio_renderer_);
}

// Test creation and interface independence of AudioCapturer.
TEST_F(AudioCoreSyncTest, CreateAudioCapturer) {
  // Validate Audio can create AudioCapturer interface.
  EXPECT_EQ(ZX_OK,
            audio_->CreateAudioCapturer(audio_capturer_.NewRequest(), true));
  EXPECT_TRUE(audio_capturer_);

  // Validate that Audio persists without AudioCapturer.
  audio_capturer_ = nullptr;
  ASSERT_TRUE(audio_);

  // Validate AudioCapturer persists after Audio is unbound.
  audio_->CreateAudioCapturer(audio_capturer_.NewRequest(), false);
  audio_ = nullptr;
  EXPECT_TRUE(audio_capturer_);
}

// Test the setting of audio output routing policy.
TEST_F(AudioCoreSyncTest, SetRoutingPolicy) {
  // Validate Audio can set last-plugged routing policy synchronously.
  EXPECT_EQ(ZX_OK,
            audio_->SetRoutingPolicy(
                fuchsia::media::AudioOutputRoutingPolicy::LAST_PLUGGED_OUTPUT));

  // Validate Audio can set all-outputs routing policy synchronously.
  EXPECT_EQ(ZX_OK,
            audio_->SetRoutingPolicy(
                fuchsia::media::AudioOutputRoutingPolicy::ALL_PLUGGED_OUTPUTS));

  // Leave this persistent systemwide setting in the default state!
  EXPECT_EQ(ZX_OK,
            audio_->SetRoutingPolicy(
                fuchsia::media::AudioOutputRoutingPolicy::LAST_PLUGGED_OUTPUT));
}

}  // namespace test
}  // namespace audio
}  // namespace media
