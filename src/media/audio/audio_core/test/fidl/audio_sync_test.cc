// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include "src/media/audio/lib/test/hermetic_audio_test.h"

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
class AudioSyncTest : public HermeticAudioTest {
 protected:
  void SetUp() override;
  void TearDown() override;

  fuchsia::media::AudioCoreSyncPtr audio_core_sync_;
  fuchsia::media::AudioRendererSyncPtr audio_renderer_sync_;
  fuchsia::media::AudioCapturerSyncPtr audio_capturer_sync_;
};

void AudioSyncTest::SetUp() {
  HermeticAudioTest::SetUp();
  environment()->ConnectToService(audio_core_sync_.NewRequest());
}

void AudioSyncTest::TearDown() {
  if (audio_renderer_sync_) {
    audio_renderer_sync_.Unbind();
  }
  if (audio_capturer_sync_) {
    audio_capturer_sync_.Unbind();
  }
  if (audio_core_sync_) {
    audio_core_sync_.Unbind();
  }

  HermeticAudioTest::TearDown();
}

//
// AudioCoreSync validation
// Tests of the synchronously-proxied Audio interface: AudioSync.
//
// Test creation and interface independence of AudioRenderer.
TEST_F(AudioSyncTest, CreateAudioRenderer) {
  // Validate Audio can create AudioRenderer interface.
  EXPECT_EQ(ZX_OK, audio_core_sync_->CreateAudioRenderer(audio_renderer_sync_.NewRequest()));

  // Validate synchronous Audio can create asynchronous AudioRenderers, too.
  fuchsia::media::AudioRendererPtr audio_renderer;
  EXPECT_EQ(ZX_OK, audio_core_sync_->CreateAudioRenderer(audio_renderer.NewRequest()));

  // Validate that Audio persists without AudioRenderer.
  // Before unbinding this, make sure it survived this far.
  EXPECT_TRUE(audio_renderer_sync_.is_bound());
  audio_renderer_sync_.Unbind();

  // Validate AudioRenderer persists after Audio is unbound.
  EXPECT_EQ(ZX_OK, audio_core_sync_->CreateAudioRenderer(audio_renderer_sync_.NewRequest()));

  // Before unbinding this, make sure it survived this far.
  EXPECT_TRUE(audio_core_sync_.is_bound());
  audio_core_sync_.Unbind();

  EXPECT_FALSE(audio_core_sync_.is_bound());
  EXPECT_TRUE(audio_renderer_sync_.is_bound());
  EXPECT_TRUE(audio_renderer.is_bound());
}

// Test creation and interface independence of AudioCapturer.
TEST_F(AudioSyncTest, CreateAudioCapturer) {
  // Validate Audio can create AudioCapturer interface.
  EXPECT_EQ(ZX_OK, audio_core_sync_->CreateAudioCapturer(true, audio_capturer_sync_.NewRequest()));

  // Validate synchronous Audio can create asynchronous AudioCapturers too.
  fuchsia::media::AudioCapturerPtr audio_capturer;
  EXPECT_EQ(ZX_OK, audio_core_sync_->CreateAudioCapturer(false, audio_capturer.NewRequest()));

  // Validate that Audio persists without AudioCapturer.
  // Before unbinding this, make sure it survived this far.
  EXPECT_TRUE(audio_capturer_sync_.is_bound());
  audio_capturer_sync_.Unbind();

  // Validate AudioCapturer persists after Audio is unbound.
  audio_core_sync_->CreateAudioCapturer(false, audio_capturer_sync_.NewRequest());

  // Before unbinding this, make sure it survived this far.
  EXPECT_TRUE(audio_core_sync_.is_bound());
  audio_core_sync_.Unbind();

  EXPECT_FALSE(audio_core_sync_.is_bound());
  EXPECT_TRUE(audio_capturer_sync_.is_bound());
  EXPECT_TRUE(audio_capturer.is_bound());
}

//
// TODO(mpuryear): "fuzz" tests (FIDL-compliant but protocol-inconsistent).
//

// Test the setting of audio output routing policy.
TEST_F(AudioSyncTest, SetRoutingPolicy) {
  // Validate Audio can set last-plugged routing policy synchronously.
  EXPECT_EQ(ZX_OK, audio_core_sync_->SetRoutingPolicy(
                       fuchsia::media::AudioOutputRoutingPolicy::LAST_PLUGGED_OUTPUT));

  // Validate Audio can set all-outputs routing policy synchronously.
  EXPECT_EQ(ZX_OK, audio_core_sync_->SetRoutingPolicy(
                       fuchsia::media::AudioOutputRoutingPolicy::ALL_PLUGGED_OUTPUTS));

  // Out-of-range enum should be blocked at sender-side.
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, audio_core_sync_->SetRoutingPolicy(
                                     static_cast<fuchsia::media::AudioOutputRoutingPolicy>(-1u)));

  // These tests should be running hermetically, but if not (if running on the
  // system's global audio_core), reset persistent system settings to defaults!
  EXPECT_EQ(ZX_OK, audio_core_sync_->SetRoutingPolicy(
                       fuchsia::media::AudioOutputRoutingPolicy::LAST_PLUGGED_OUTPUT));
  EXPECT_TRUE(audio_core_sync_.is_bound());
}

}  // namespace media::audio::test
