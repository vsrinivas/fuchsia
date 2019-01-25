// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>

#include <lib/gtest/real_loop_fixture.h>

#include "lib/component/cpp/environment_services_helper.h"

namespace media::audio::test {

//
// AudioSyncTest
//
// We expect the async and sync interfaces to track each other exactly -- any
// behavior otherwise is a bug in core FIDL. These tests were only created to
// better understand how errors manifest themselves when using sync interfaces.
// In short, further testing of the sync interfaces (over and above any testing
// done on the async interfaces) should not be needed.
//
class AudioSyncTest : public gtest::RealLoopFixture {
 protected:
  void SetUp() override {
    ::gtest::RealLoopFixture::SetUp();

    environment_services_ = component::GetEnvironmentServices();
    environment_services_->ConnectToService(audio_sync_.NewRequest());
    ASSERT_TRUE(audio_sync_.is_bound())
        << "Unable to bind to AudioSync interface";
  }

  std::shared_ptr<component::Services> environment_services_;
  fuchsia::media::AudioSyncPtr audio_sync_;
  fuchsia::media::AudioRendererSyncPtr audio_renderer_sync_;
  fuchsia::media::AudioCapturerSyncPtr audio_capturer_sync_;
};

//
// AudioCoreSync validation
// Tests of the synchronously-proxied Audio interface: AudioSync.
//
// Test creation and interface independence of AudioRenderer.
TEST_F(AudioSyncTest, CreateAudioRenderer) {
  // Validate Audio can create AudioRenderer interface.
  EXPECT_EQ(ZX_OK, audio_sync_->CreateAudioRenderer(
                       audio_renderer_sync_.NewRequest()));
  EXPECT_TRUE(audio_renderer_sync_.is_bound());
  EXPECT_TRUE(audio_sync_.is_bound());

  // Validate that Audio persists without AudioRenderer.
  audio_renderer_sync_.Unbind();
  EXPECT_TRUE(audio_sync_.is_bound());
  EXPECT_FALSE(audio_renderer_sync_.is_bound());

  // Validate AudioRenderer persists after Audio is unbound.
  EXPECT_EQ(ZX_OK, audio_sync_->CreateAudioRenderer(
                       audio_renderer_sync_.NewRequest()));
  audio_sync_.Unbind();
  EXPECT_TRUE(audio_renderer_sync_.is_bound());
  EXPECT_FALSE(audio_sync_.is_bound());
}

// Test creation and interface independence of AudioCapturer.
TEST_F(AudioSyncTest, CreateAudioCapturer) {
  // Validate Audio can create AudioCapturer interface.
  EXPECT_EQ(ZX_OK, audio_sync_->CreateAudioCapturer(
                       audio_capturer_sync_.NewRequest(), true));
  EXPECT_TRUE(audio_capturer_sync_.is_bound());
  EXPECT_TRUE(audio_sync_.is_bound());

  // Validate that Audio persists without AudioCapturer.
  audio_capturer_sync_.Unbind();
  EXPECT_TRUE(audio_sync_.is_bound());
  EXPECT_FALSE(audio_capturer_sync_.is_bound());

  // Validate AudioCapturer persists after Audio is unbound.
  audio_sync_->CreateAudioCapturer(audio_capturer_sync_.NewRequest(), false);
  audio_sync_.Unbind();
  EXPECT_TRUE(audio_capturer_sync_.is_bound());
  EXPECT_FALSE(audio_sync_.is_bound());
}

// Test the setting of audio output routing policy.
TEST_F(AudioSyncTest, SetRoutingPolicy) {
  // Validate Audio can set last-plugged routing policy synchronously.
  EXPECT_EQ(ZX_OK,
            audio_sync_->SetRoutingPolicy(
                fuchsia::media::AudioOutputRoutingPolicy::LAST_PLUGGED_OUTPUT));

  // Validate Audio can set all-outputs routing policy synchronously.
  EXPECT_EQ(ZX_OK,
            audio_sync_->SetRoutingPolicy(
                fuchsia::media::AudioOutputRoutingPolicy::ALL_PLUGGED_OUTPUTS));

  // Leave this persistent systemwide setting in the default state!
  EXPECT_EQ(ZX_OK,
            audio_sync_->SetRoutingPolicy(
                fuchsia::media::AudioOutputRoutingPolicy::LAST_PLUGGED_OUTPUT));
}

}  // namespace media::audio::test
